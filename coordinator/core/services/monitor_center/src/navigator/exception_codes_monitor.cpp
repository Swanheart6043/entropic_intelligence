#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "exception_codes_monitor.hpp"
#include "broadcast_template.hpp"

LiDarMonitor::LiDarMonitor() {
    socket_to_audio.connect("ipc://@edge_gateway_push_pull");
    socket_to_web.connect("tcp://127.0.0.1:2007");
    
    websocket = std::make_unique<WebSocketClient>("10.10.10.10:8089/gs-robot/notice/errCode");    
    WebSocketClient::HeartbeatConfig hb;
    hb.enable_heartbeat = false;
    websocket->set_heartbeat_config(hb);
    websocket->set_message_callback([this](const std::string& message) {listening(message);});
}

LiDarMonitor::~LiDarMonitor() {
    stop();
}

void LiDarMonitor::start() {
    if (running) {
        SPDLOG_WARN("激光雷达监控线程，已经在启动了");
        return;
    }
    running = true;
    websocket->start_client();
}

void LiDarMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("激光雷达监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    websocket->stop_client();
}

void LiDarMonitor::listening(const std::string& message) {
    try {
        SPDLOG_DEBUG("导航异常码消息: {}", message);
        if (message.find("40107") != std::string::npos) {
            handle_someone_on_the_lid();
            return;
        }
        if (message.find("40108") != std::string::npos) {
            handle_laser_anomalies();
            return;
        }
        if (message.find("40109") != std::string::npos) {
            handle_gyroscope_malfunctions();
            return;
        }
        if (message.find("40111") != std::string::npos) {
            handle_ultrasound_anomalies();
            return;
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("解析导航异常码异常: {}", e.what());
    }
}

// 后盖有人
void LiDarMonitor::handle_someone_on_the_lid() {
    BroadcastTemplate broadcast_template("lidar.someone_on_top");
    auto content = broadcast_template.get_content("注意，请不要靠在我附近以免发生意外");

    nlohmann::json params = {
        {"command", "broadcast-single-text"},
        {"content", content}
    };
    auto to_audio_result = socket_to_audio.send(
        zmq::buffer(params.dump()), 
        zmq::send_flags::dontwait
    );
    if (to_audio_result) {
        SPDLOG_INFO("发送给音频成功: {}", params.dump());
    } else {
        SPDLOG_WARN("发送给音频失败 edge_gateway 可能未运行，消息已丢弃");
    }

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "40107"}}}
    };
    auto to_web_result = socket_to_web.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
    if (to_web_result) {
        SPDLOG_INFO("发送给web成功: {}", message.dump());
    } else {
        SPDLOG_WARN("发送给web失败 web_gateway 可能未运行，消息已丢弃");
    }
}

// 激光连接异常
void LiDarMonitor::handle_laser_anomalies() {
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "40108"}}}
    };
    auto result = socket_to_web.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
    if (result) {
        SPDLOG_INFO("发送给web成功: {}", message.dump());
    } else {
        SPDLOG_WARN("发送给web失败 web_gateway 可能未运行，消息已丢弃");
    }
}

// 陀螺仪连接异常
void LiDarMonitor::handle_gyroscope_malfunctions() {
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "40109"}}}
    };
    auto result = socket_to_web.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
    if (result) {
        SPDLOG_INFO("发送给web成功: {}", message.dump());
    } else {
        SPDLOG_WARN("发送给web失败 web_gateway 可能未运行，消息已丢弃");
    }
}

// 超声波连接异常
void LiDarMonitor::handle_ultrasound_anomalies() {
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "40111"}}}
    };
    auto result = socket_to_web.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
    if (result) {
        SPDLOG_INFO("发送给web成功: {}", message.dump());
    } else {
        SPDLOG_WARN("发送给web失败 web_gateway 可能未运行，消息已丢弃");
    }
}