#pragma once
#include <iostream>
#include <string>
#include <zmq.hpp>
#include <thread>
#include <chrono>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class MediaHandler {
public: 
    MediaHandler();
    void handle_broadcast_text(WebSocketClient& websocket_connect, const std::string& message);
    void handle_listening_and_talking(WebSocketClient& websocket_connect, const std::string& message);
    void handle_screen_display(WebSocketClient& websocket_connect, const std::string& message);
    void handle_switch(WebSocketClient& websocket_connect, const std::string& message);
    void handle_sos_end(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config_ = Config::getInstance();

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_task_center_internal{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_task_center_human{ctx, zmq::socket_type::push};
};