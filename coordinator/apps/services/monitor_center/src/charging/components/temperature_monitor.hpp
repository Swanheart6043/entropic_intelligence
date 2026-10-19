#pragma once
#include <atomic>
#include <zmq.hpp>
#include "config_parser.hpp"

class TemperatureMonitor {
public:
    TemperatureMonitor();
    ~TemperatureMonitor();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();

    std::atomic<bool> running{false};
    std::thread worker_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_from_control{ctx, ZMQ_SUB};
    zmq::socket_t socket_to_task_center{ctx, zmq::socket_type::push};

    void temperature_loop();
    void handle_battery_too_hot_during_charge();
};