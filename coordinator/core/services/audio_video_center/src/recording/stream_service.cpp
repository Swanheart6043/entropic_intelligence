#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "stream_service.hpp"

RecordingStreamingService::RecordingStreamingService() {
    socket.bind("ipc://@recording_center_file_streaming");
}

RecordingStreamingService::~RecordingStreamingService() {
    stop();
}

void RecordingStreamingService::start() {
    if (running) {
        SPDLOG_WARN("文件推流服务已经在运行了");
        return;
    }
    running = true;
    worker_thread = std::thread(&RecordingStreamingService::message_loop, this);
    SPDLOG_INFO("文件推流服务启动");
}

void RecordingStreamingService::stop() {
    if (!running) return;
    running = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    SPDLOG_INFO("文件推流服务停止");
}

void RecordingStreamingService::message_loop() {
    while (true) {
        try {
            zmq::message_t zmq_message;
            auto recv_result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!recv_result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("录像文件推流线程收到消息: {}", message_string);

            nlohmann::json message = nlohmann::json::parse(message_string);
            std::string type = message.at("type").get<std::string>();
            std::string channel = message.at("channel").get<std::string>();
            std::string file_name = message.at("file_name").get<std::string>();
            
            if (type == "recording-streaming") {
                enable_streaming(channel, file_name);
                continue;
            }
            if (type == "disable-streaming") {
                disable_streaming(channel);
                continue;
            }
            SPDLOG_WARN("未知消息: {}", type);

        } catch (const std::exception& e) {
            SPDLOG_ERROR("录像文件推流线程异常: {}", e.what());
        }
    }
}

void RecordingStreamingService::enable_streaming(const std::string& channel, const std::string& file_name) {
    streamer_.reset(); // 停掉上一个（如果有）
    streamer_ = std::make_unique<FileStreamer>();
    streamer_->set_finish_callback([this] {
        SPDLOG_INFO("文件推流完成");
    });
    std::string channel_name = match_channel(channel);
    std::string input_path = "/datas/recordings/" + channel + "/" + file_name;
    std::string output_suffix = "/" + config->get_sn() + "/" + file_name + "_" + channel_name;
    std::string output_url = config->get_platform_rtsp_base_url() + output_suffix;
    if (!streamer_->start(input_path, output_url)) {
        streamer_.reset();
    }
}

void RecordingStreamingService::disable_streaming(const std::string& channel) {
    streamer_.reset();
}

std::string RecordingStreamingService::match_channel(const std::string& name) {
    if (name == "front") {
        return "1";
    }
    if (name == "high_definition") {
        return "2";
    }
    if (name == "infrared") {
        return "3";
    }
    if (name == "rear") {
        return "4";
    }
    if (name == "left") {
        return "5";
    }
    if (name == "right") {
        return "6";
    }
    return "";
}
