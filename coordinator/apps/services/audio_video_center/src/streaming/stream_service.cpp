#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "stream_service.hpp"

RtspStreamingService::RtspStreamingService() {
    socket.bind("ipc://@audio_video_pull");
}

RtspStreamingService::~RtspStreamingService() {
    stop();
}

void RtspStreamingService::start() {
    if (running) {
        SPDLOG_WARN("StreamingService already running");
        return;
    }
    running = true;
    worker_thread = std::thread(&RtspStreamingService::message_loop, this);
    SPDLOG_INFO("StreamingService started");
}

void RtspStreamingService::stop() {
    if (!running) return;
    running = false;
    // 停止所有推流
    for (auto& [channel, streaming_session] : streamer_map) {
        if (streaming_session.streamer && streaming_session.streamer->is_running()) {
            streaming_session.streamer->stop();
        }
    }
    streamer_map.clear();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    SPDLOG_INFO("StreamingService stopped");
}

void RtspStreamingService::message_loop() {
    while (true) {
        try {
            zmq::message_t zmq_message;
            auto recv_result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!recv_result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("推流线程收到消息: {}", message_string);

            nlohmann::json message = nlohmann::json::parse(message_string);
            std::string channel = message.at("channel").get<std::string>();
            std::string type = message.at("type").get<std::string>();
            
            if (type == "enable-streaming") {
                handle_enable_streaming(channel);
                check_streaming_timeout();
            } else if (type == "disable-streaming") {
                handle_disable_streaming(channel);
            } else {
                SPDLOG_WARN("Unknown message type: {}", type);
            }

        } catch (const std::exception& e) {
            SPDLOG_ERROR("推流线程异常: {}", e.what());
        }
    }
}

void RtspStreamingService::handle_enable_streaming(const std::string& channel) {
    // 检查是否已在推流
    if (streamer_map.count(channel) && streamer_map[channel].streamer->is_running()) {
        SPDLOG_INFO("channel: {} 已在推流", channel);
        return;
    }
    
    // 查找channel配置
    auto it = channel_map.find(channel);
    if (it == channel_map.end()) {
        SPDLOG_ERROR("未知channel不处理: {}", channel);
        return;
    }
    
    // 创建streamer
    auto streamer = std::make_unique<RtspStreamer>();
    std::string input_url = "rtsp://admin:heyn_888@" + it->second.ip + ":" + it->second.port + it->second.path;
    std::string output_url = config->get_platform_rtsp_base_url() + it->second.output_path;
    
    if (streamer->start(input_url, output_url)) {
        streamer_map[channel] = StreamingSession{
            std::move(streamer),
            std::chrono::steady_clock::now(),
        };
        SPDLOG_INFO("Channel: {} 开启推流成功", channel);
    } else {
        SPDLOG_ERROR("channel: {} 开启推流失败", channel);
    }
}

void RtspStreamingService::handle_disable_streaming(const std::string& channel) {
    auto it = streamer_map.find(channel);
    if (it != streamer_map.end() && it->second.streamer->is_running()) {
        it->second.streamer->stop();
        streamer_map.erase(it);
        SPDLOG_INFO("Channel: {} 已停止推流", channel);
    } else {
        SPDLOG_INFO("channel: {} 未在推流", channel);
    }
}

void RtspStreamingService::check_streaming_timeout() {
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = streamer_map.begin(); it != streamer_map.end(); ) {
        auto& session = it->second;
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(now - session.start_time).count();
        
        if (elapsed >= MAX_STREAMING_MINUTES) {
            SPDLOG_INFO("Channel: {} 推流已达{}分钟，自动停止", it->first, elapsed);
            if (session.streamer && session.streamer->is_running()) {
                session.streamer->stop();
            }
            it = streamer_map.erase(it);
        } else {
            ++it;
        }
    }
}
