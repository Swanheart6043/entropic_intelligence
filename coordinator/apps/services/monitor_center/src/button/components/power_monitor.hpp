#pragma once
#include <zmq.hpp>
#include "config_parser.hpp"
#include "sos_monitor.hpp"

class PowerButtonMonitor {
public:
    explicit PowerButtonMonitor();
    void start();

private:
    Config* config = Config::getInstance();
    
    zmq::context_t zmq_ctx{1};
    zmq::socket_t zmq_socket_power{zmq_ctx, ZMQ_SUB};

    void check_power_button(bool power_button);
};