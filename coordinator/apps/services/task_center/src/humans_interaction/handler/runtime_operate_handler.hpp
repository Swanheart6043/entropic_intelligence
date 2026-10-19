#pragma once
#include <string>
#include <zmq.hpp>
#include "config_parser.hpp"

class RuntimeOperateHandler {
public: 
    RuntimeOperateHandler(zmq::socket_t& zmq_socket);
    ~RuntimeOperateHandler();

    void handle_mode_change(const std::string& message);
    void handle_pause_recovery(const std::string& message);
    void handle_to_charge(const std::string& message);

private:
    Config* config = Config::getInstance();
    zmq::socket_t& zmq_socket_;
};