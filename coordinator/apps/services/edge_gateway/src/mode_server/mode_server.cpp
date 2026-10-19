#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "state_subscriber.hpp"
#include "common/websocket_manager.hpp"
#include "mode_server.hpp"

EdgeModeServer::EdgeModeServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx) : robot_info_(robot_info), robot_info_mtx_(robot_info_mtx) {
    int timeout = 1000;
    socket_sub.setsockopt(ZMQ_SUBSCRIBE, "mode_info", strlen("mode_info"));
    socket_sub.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket_sub.connect("tcp://127.0.0.1:4096");
}

EdgeModeServer::~EdgeModeServer() {
    stop();
}

void EdgeModeServer::start() {
    if (running) return;
    running = true;
    sub_thread = std::thread(&EdgeModeServer::listen_mode, this);
    init_thread = std::thread(&EdgeModeServer::init_mode, this);
}

void EdgeModeServer::stop() {
    if (!running) return;
    running = false;
    if (sub_thread.joinable()) sub_thread.join();
    if (init_thread.joinable()) init_thread.join();
}

void EdgeModeServer::init_mode() {
    while (running) {
        try {
            std::string mode = StateSubscriber::getInstance().get_mode();
            if (mode.empty()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(robot_info_mtx_);
                robot_info_["mode"] = mode;
            }
            break;
        } catch(const std::exception& e) {
            SPDLOG_ERROR("初始模式异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void EdgeModeServer::listen_mode() {
    while (running) {
        try {
            zmq::message_t topic, zmq_message;
            if (!socket_sub.recv(topic, zmq::recv_flags::none)) continue;
            SPDLOG_INFO("收到模式topic: [{}]", std::string(static_cast<char*>(topic.data()), topic.size()));
            if (!socket_sub.recv(zmq_message, zmq::recv_flags::none)) continue;
            std::string mode_string(static_cast<char*>(zmq_message.data()), zmq_message.size());

            SPDLOG_INFO("收到模式: {}", mode_string);
            {
                std::lock_guard<std::mutex> lock(robot_info_mtx_);
                robot_info_["mode"] = mode_string;
            }
        } catch(const std::exception& e) {
            SPDLOG_ERROR("监听模式异常: {}", e.what());
        }
    }
}