#pragma once
#include <zmq.hpp>
#include "config_parser.hpp"

class StayMonitor {
public:
    explicit StayMonitor();
    ~StayMonitor();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();
    std::atomic<bool> running{false};
    std::thread worker_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_power{ctx, ZMQ_SUB};
    zmq::socket_t socket_web{ctx, zmq::socket_type::push};

    bool is_auto_charging = false;
    bool is_manual_charging = false;
    bool is_staying = false; // 是否正在停留
    bool is_run = false;
    std::chrono::steady_clock::time_point stay_start_time;

    std::chrono::steady_clock::time_point last_report_time;
    const int REPORT_INTERVAL_SECONDS = 5 * 60;  // 上报间隔
    const int STAY_THRESHOLD_SECONDS = 5 * 60; // 5分钟阈值

    void monitoring();
    void handle_long_term_stays();
    void handle_not_long_term_stays();
};