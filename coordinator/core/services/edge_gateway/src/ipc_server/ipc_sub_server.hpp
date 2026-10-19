#pragma once
#include <thread>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "components/broadcast_controller.hpp"

class TaskCenterServer {
public:
    TaskCenterServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx);
    ~TaskCenterServer();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::mutex& robot_info_mtx_;
    nlohmann::json& robot_info_;

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::sub};

    BroadcastController broadcast_controller;

    void message_loop();
    void match_message(const std::string& message_string);
};