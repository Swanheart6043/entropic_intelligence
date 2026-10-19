#pragma once
#include <string>
#include <zmq.hpp>
#include "config_parser.hpp"

class EquipmentHandler {
public: 
    EquipmentHandler(zmq::socket_t& zmq_socket);
    void handle(const std::string& message);
    void handle_lights(const std::string& message);
    void handle_wipers(const std::string& message);
    void release_emergency_stop();
    void match_reboot_operate(const std::string& message);

private:
    Config* config = Config::getInstance();
    zmq::socket_t& zmq_socket_;
};