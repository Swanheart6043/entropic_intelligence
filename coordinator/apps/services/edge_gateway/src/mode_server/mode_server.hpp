#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <zmq.hpp>

class EdgeModeServer {
public:
    EdgeModeServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx);
    ~EdgeModeServer();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    std::thread sub_thread;
    std::thread init_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_sub{ctx, ZMQ_SUB};

    nlohmann::json& robot_info_;
    std::mutex& robot_info_mtx_;

    void init_mode();
    void listen_mode();
};