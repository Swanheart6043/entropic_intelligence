#pragma once
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <nlohmann/json.hpp>
#include "httplib.hpp"

class EdgeHttpServer {
public:
    EdgeHttpServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx);
    ~EdgeHttpServer();
    void start();
    void stop();

private:
    void http_loop();

    std::atomic<bool> running{false};
    std::thread worker_thread;
    httplib::Server server_;   // 直接持有，stop() 可访问

    nlohmann::json& robot_info_;
    std::mutex& robot_info_mtx_;
};