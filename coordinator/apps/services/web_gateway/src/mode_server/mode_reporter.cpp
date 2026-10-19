#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "mode_reporter.hpp"

ModeReporter::ModeReporter(WebSocketClient& websocket_connect) : websocket_connect_(websocket_connect) {
    socket.setsockopt(ZMQ_SUBSCRIBE, topic.c_str(), strlen(topic.c_str()));
    socket.connect("tcp://127.0.0.1:4096");
}

ModeReporter::~ModeReporter() {
    stop();
}

void ModeReporter::start() {
    if (report_thread_.joinable()) {
        SPDLOG_WARN("ModeReporter已在运行中");
        return;
    }
    should_stop_ = false;
    report_thread_ = std::thread(&ModeReporter::report_loop, this);
}

void ModeReporter::stop() {
    should_stop_ = true;
    cv_.notify_all();
    if (report_thread_.joinable()) report_thread_.join();
}

void ModeReporter::report_loop() {
    while (true) {
        // 接收topic
        zmq::message_t topic;
        auto topic_result = socket.recv(topic, zmq::recv_flags::none);
        if (!topic_result) continue;

        // 接收数据
        zmq::message_t zmq_message;
        auto message_result = socket.recv(zmq_message, zmq::recv_flags::none);
        if (!message_result) continue;
        std::string mode_string(static_cast<char*>(zmq_message.data()), zmq_message.size());

        // 只在模式真正变化时才发送
        if (mode_string == last_mode) {
            SPDLOG_INFO("模式未变化，不发送: {}", mode_string);
            continue;
        }
        last_mode = mode_string;

        nlohmann::json params = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"}, 
                {"Sn", config->get_sn()}, 
                {"Time", std::time(nullptr)}
            }},
            {"CurrentMode", mode_string == "auto" ? 0 : 1},
            {"CurrentTask", {{"TaskInfo", nlohmann::json::object()}}}
        };
        websocket_connect_.send_message(params.dump());
        SPDLOG_INFO("已发送模式到平台: {}", params.dump());
    }
}