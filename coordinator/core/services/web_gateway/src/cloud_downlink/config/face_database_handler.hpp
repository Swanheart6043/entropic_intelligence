#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class FaceDatabaseHandler {
public: 
    FaceDatabaseHandler();
    void handle(WebSocketClient& websocket_connect, const std::string& message);
    void answer(WebSocketClient& websocket_connect, const std::string& message);
private:
    Config* config_ = Config::getInstance();
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::req};
};