#pragma once
#include <atomic>
#include <zmq.hpp>
#include "config_parser.hpp"

class PowerMonitor {
public:
    PowerMonitor();
    ~PowerMonitor();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();
    std::atomic<bool> running{false};
    std::thread worker_thread;

    zmq::context_t ctx{1};
    zmq::socket_t zmq_socket_power{ctx, ZMQ_SUB};
    zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_task_center{ctx, zmq::socket_type::push};

    int battery = 0;
    int voltage = 0;
    bool charging = false;
    int battery_temperature = 0;

    bool low_battery_charge_triggered = false; // 20% 回充

    void monitoring();
    void check_low_battery_warning();
    void check_low_battery_go_back_charge();
    void check_battery_overheating();
};
