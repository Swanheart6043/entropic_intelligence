#pragma once
#include <string>
#include <thread>
#include <chrono>
#include <zmq.hpp>
#include "config_parser.hpp"
#include "component/ptz_controller.hpp"

class PtzOperateHandler {
public: 
    PtzOperateHandler(zmq::socket_t& zmq_socket);
    void match_ptz_operate(const std::string& message);
    void match_preset_position_operate(const std::string& message);
    void create_preset_position(const std::string&);
    void update_preset_position(const std::string&, const std::string&);
    
private:
    Config* config = Config::getInstance();
    zmq::socket_t& zmq_socket_;
    PTZController ptz_controller;
    void match_preset_position(const std::string& value);
};