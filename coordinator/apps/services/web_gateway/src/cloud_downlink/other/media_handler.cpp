#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "media_handler.hpp"

MediaHandler::MediaHandler() {
    int linger = 1000;
	int sndtimeo = 1000;
    int sndhwm = 100;
    
    socket_to_edge_gateway.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_edge_gateway.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_edge_gateway.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_edge_gateway.connect("ipc://@from_web_gateway"); // 连接

    socket_to_task_center_internal.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_task_center_internal.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_task_center_internal.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_task_center_internal.connect("tcp://127.0.0.1:8223");

    socket_to_task_center_human.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_task_center_human.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_task_center_human.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_task_center_human.connect("tcp://127.0.0.1:4004");
}

void MediaHandler::handle_broadcast_text(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {{"CmdType", "VI_ROBOT_SOUNDTEXT_SET"},{"Sn", config_->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 发送给边缘网关
        socket_to_edge_gateway.send(zmq::buffer(message), zmq::send_flags::dontwait);
		SPDLOG_INFO("通知边缘网关 {}", message);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("通知边缘网关异常: {}", e.what());
    }
}

void MediaHandler::handle_listening_and_talking(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {{"CmdType", "VoiceInteractionType_Set"},{"Sn", config_->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 发送给边缘网关
        socket_to_edge_gateway.send(zmq::buffer(message), zmq::send_flags::dontwait);
		SPDLOG_INFO("通知边缘网关开启监听对讲: {}", message);
    } catch(const std::exception& e) {
		SPDLOG_ERROR("通知边缘网关开启监听对讲异常: {}", e.what());
	}
}

void MediaHandler::handle_screen_display(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {{"CmdType", "TS_Slogan_Config_Info"},{"Sn", config_->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 发送给边缘网关
        socket_to_edge_gateway.send(zmq::buffer(message), zmq::send_flags::dontwait);
        SPDLOG_INFO("通知边缘网关显示屏标语");
    } catch(const std::exception& e) {
        SPDLOG_ERROR("通知边缘网关显示屏标语异常: {}", e.what());
    }
}

void MediaHandler::handle_switch(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {{"CmdType", "RC_RobotEnable_Set"},{"Sn", config_->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        auto type = nlohmann::json::parse(message).at("Switch").value("Type", "");
        // 音量
        if (type == "5") {
            // 发送给边缘网关
            socket_to_edge_gateway.send(zmq::buffer(message), zmq::send_flags::dontwait);
        } else {
            // 发送给任务中心
            socket_to_task_center_human.send(zmq::buffer(message), zmq::send_flags::dontwait);
        }
    } catch(const std::exception& e) {
        SPDLOG_ERROR("通知边缘网关显示屏标语异常: {}", e.what());
    }
}

void MediaHandler::handle_sos_end(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {{"CmdType", "Call_SOS_hangUp"},{"Sn", config_->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 发送给边缘网关
        socket_to_edge_gateway.send(zmq::buffer(message), zmq::send_flags::dontwait);
        
        // 发送给任务中心
        nlohmann::json params = {{"command", "sos-recovery"},{"content", ""}};
        socket_to_task_center_internal.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
    } catch(const std::exception& e) {
        SPDLOG_ERROR("通知边缘网关显示屏标语异常: {}", e.what());
    }
}