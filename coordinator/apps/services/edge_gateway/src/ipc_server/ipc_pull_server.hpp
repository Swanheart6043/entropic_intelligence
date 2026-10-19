#pragma once
#include <thread>
#include <zmq.hpp>
#include "components/broadcast_controller.hpp"

class EdgeIpcServer {
public:
    EdgeIpcServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx);
    ~EdgeIpcServer();
    void start();
    void stop();

private:
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    
    std::atomic<bool> running{false};
    std::thread worker_thread;

    nlohmann::json& robot_info_;
    std::mutex& robot_info_mtx_;

    BroadcastController broadcast_controller;

    void message_loop();
    void match_message(const std::string& message_string);
};