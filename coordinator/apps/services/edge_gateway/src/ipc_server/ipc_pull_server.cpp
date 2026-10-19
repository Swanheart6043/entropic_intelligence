#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "common/websocket_manager.hpp"
#include "state_subscriber.hpp"
#include "ipc_pull_server.hpp"

EdgeIpcServer::EdgeIpcServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx) : robot_info_(robot_info), robot_info_mtx_(robot_info_mtx) {
    int timeout = 1000;
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket.bind("ipc://@edge_gateway_push_pull");
}

EdgeIpcServer::~EdgeIpcServer() {
    stop();
}

void EdgeIpcServer::start() {
    if (running) {
        SPDLOG_WARN("边缘网关IPC服务, 已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&EdgeIpcServer::message_loop, this);
}

void EdgeIpcServer::stop() {
    if (!running) {
        SPDLOG_WARN("边缘网关IPC服务, 没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void EdgeIpcServer::message_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("边缘网关IPC服务收到消息: {}", message_string);
            match_message(message_string);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("边缘网关IPC服务异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
    }
}

void EdgeIpcServer::match_message(const std::string& message_string) {
    nlohmann::json message = nlohmann::json::parse(message_string);
    std::string command = message.at("command").get<std::string>();

    if (command == "broadcast-single-text") {
        std::string content = message.at("content").get<std::string>();
        broadcast_controller.broadcast_text(content, "single", "0");
        return;
    }
    if (command == "alarm") {
        nlohmann::json state = StateSubscriber::getInstance().get_full_state();
        auto gps = state.at("gps");

        std::string type = message.at("type").get<std::string>();
        std::string content = message.at("content").get<std::string>();
        nlohmann::json payload = {
            {"command", "alarm"}, 
            {"type", type}, 
            {"content", content},
            {"gps", gps}
        };
        WebSocketManager::getInstance().broadcast(payload.dump());
        return;
    }
    if (command == "sos") {
        nlohmann::json payload = {{"command", "sos"}, {"content", ""}};
        WebSocketManager::getInstance().broadcast(payload.dump());
        return;
    }
}