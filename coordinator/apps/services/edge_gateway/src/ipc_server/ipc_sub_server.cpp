#include <iostream>
#include <spdlog/spdlog.h>
#include "common/websocket_manager.hpp"
#include "ipc_sub_server.hpp"

TaskCenterServer::TaskCenterServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx) : robot_info_(robot_info), robot_info_mtx_(robot_info_mtx) {
    socket.setsockopt(ZMQ_SUBSCRIBE, "", strlen(""));
    socket.bind("ipc://@edge_gateway_sub_task_center");
}

TaskCenterServer::~TaskCenterServer() {
    stop();
}

void TaskCenterServer::start() {
    if (running) {
        SPDLOG_WARN("任务中心监听线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&TaskCenterServer::message_loop, this);
}

void TaskCenterServer::stop() {
    if (!running) return;
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void TaskCenterServer::message_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("任务中心交互线程，收到消息: {}", message_string);
            match_message(message_string);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("任务中心交互线程，异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
    }
}

void TaskCenterServer::match_message(const std::string& message_string) {
    nlohmann::json message = nlohmann::json::parse(message_string);
    std::string topic = message["topic"];

    if (topic == "volume_adjustment") {
        auto volume = message.value("volume", "0");
        broadcast_controller.control_volume(volume);
        return;
    }
    if (topic == "ready_patrol") {
        SPDLOG_INFO("收到ready_patrol");
        std::lock_guard<std::mutex> lk(robot_info_mtx_);
        robot_info_.at("totalPatrolPointCount") = std::to_string(message["points"].get<int>());
        return;
    }
    if (topic == "arrival_point") {
        SPDLOG_INFO("arrival_point");
        std::lock_guard<std::mutex> lk(robot_info_mtx_);
        robot_info_.at("patrolledPointCount") = std::to_string(message["point_reached"].get<int>());
        robot_info_.at("historyPatrolPointCount") = std::to_string(message["history_point_count"].get<int>());
        return;
    }
    if (topic == "arrival_nav_custom_point") {
        nlohmann::json payload = {
            {"command", "arrival-point"},
            {"x", message.value("x", "0")},
            {"y", message.value("y", "0")},
            {"angle", message.value("angle", "0")}
        };
        WebSocketManager::getInstance().broadcast(payload.dump());
        return;
    }
}