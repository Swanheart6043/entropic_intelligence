#pragma once
#include <thread>
#include <zmq.hpp>
#include "config_parser.hpp"
#include "./components/sos_monitor.hpp"
#include "./components/stop_monitor.hpp"
#include "./components/power_monitor.hpp"

class ButtonMonitor {
public:
    ButtonMonitor();
    ~ButtonMonitor();
    void start();
    void stop();

private:
    void monitoring();
    void message_loop();
    void match_message(const std::string& message_string);
    void sos_loop();
    void stop_loop();
    void power_loop();

    Config* config = Config::getInstance();
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::sub};
    zmq::socket_t zmq_socket_power{ctx, ZMQ_SUB};
    zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_task_center{ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_task_center_ipc{ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_android{ctx, zmq::socket_type::push};

    std::atomic<bool> running{false};
    std::thread sos_button_thread;
    std::thread stop_button_thread;
    std::thread power_button_thread;
};