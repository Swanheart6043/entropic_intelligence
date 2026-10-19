#pragma once
#include <zmq.hpp>
#include "config_parser.hpp"
#include "sos_monitor.hpp"

class StopButtonMonitor {
public:
    explicit StopButtonMonitor();
    void start();

private:
    Config* config = Config::getInstance();
    
    zmq::context_t zmq_ctx{1};
    zmq::socket_t zmq_socket_power{zmq_ctx, ZMQ_SUB};
    zmq::socket_t zmq_socket_web{zmq_ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_task_center{zmq_ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_task_center_ipc{zmq_ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_android{zmq_ctx, zmq::socket_type::push};

    void check_stop_button(bool stop_button);
};