#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "common/websocket_manager.hpp"
#include "web_gateway_server.hpp"

WebGatewayServer::WebGatewayServer() {
    socket.bind("ipc://@from_web_gateway");
}

WebGatewayServer::~WebGatewayServer() {
    stop();
}

void WebGatewayServer::start() {
    if (running) {
        SPDLOG_WARN("web网关监听线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&WebGatewayServer::message_loop, this);
}

void WebGatewayServer::stop() {
    
}

void WebGatewayServer::message_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("web网关监听线程，收到消息: {}", message_string);
            match_message(message_string);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("web网关监听线程，异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
    }
}

void WebGatewayServer::match_message(const std::string& message_string) {
    nlohmann::json message = nlohmann::json::parse(message_string);
    std::string command = message.at("Header").at("CmdType");

    // 发送SOS挂断
    if (command == "Call_SOS_hangUp") {
        WebSocketManager::getInstance().broadcast(message);
        return;
    }
    // 显示屏标语
    if (command == "TS_Slogan_Config_Info") {
        auto request = message.at("Request");
        nlohmann::json response = {
            {"command", "display-slogan"},
            {"content", ""} 
        };
        response.at("content") = request;
        WebSocketManager::getInstance().broadcast(response.dump());
        return;
    }
    // 广播文字
    if (command == "VI_ROBOT_SOUNDTEXT_SET") {
        nlohmann::json response = {
            {"command", "broadcast-text"},
            {"content", {
                {"Soundtext", message["Soundtext"]},
                {"Priority", message["Priority"]},
                {"BroadcastCnt", message["BroadcastCnt"]},
                {"BroadcastInv", message["BroadcastInv"]},
                {"Duration", message["Duration"]}
            }}
        };
        WebSocketManager::getInstance().broadcast(response.dump());
        return;
    }
    // 音量
    if (command == "RC_RobotEnable_Set") {
        const auto& switch_info = message.at("Switch");
        if (switch_info.at("Type") != "5") {
            SPDLOG_WARN("忽略非音量开关指令: {}", switch_info.dump());
            return;
        }

        const auto& value = switch_info.at("Value");
        std::string volume;
        if (value.is_string()) {
            volume = value.get<std::string>();
        } else if (value.is_number()) {
            volume = value.dump();
        } else {
            SPDLOG_WARN("音量值必须是字符串或数字: {}", value.dump());
            return;
        }
        if (volume.empty()) {
            SPDLOG_WARN("音量值不能为空");
            return;
        }

        broadcast_controller.control_volume(volume);
        return;
    }
    // 监听对讲
    if (command == "VoiceInteractionType_Set") {
        nlohmann::json voice_interaction_type = message.at("VoiceInteractionType");
        nlohmann::json status = message.at("Status");
        nlohmann::json response = {
            {"command", "audio"},
            {"content", {{"voiceType", voice_interaction_type == 1 ? "talk" : "listening"}, {"status", status}}}
        };
        WebSocketManager::getInstance().broadcast(response.dump());
        return;
    }
    // 纯粹转发
    if (command == "forward") {
        nlohmann::json payload = {{"command", "forward"}, {"content", message}};
        WebSocketManager::getInstance().broadcast(payload.dump());
        return;
    }
    WebSocketManager::getInstance().broadcast(message);
}
