#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <zmq.hpp>
#include "components/broadcast_controller.hpp"

class WebGatewayServer {
public:
    WebGatewayServer();
    ~WebGatewayServer();
    void start();
    void stop();

private:
    void message_loop();
    void match_message(const std::string& message_string);

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    BroadcastController broadcast_controller;
    
    std::atomic<bool> running{false};
    std::thread worker_thread;
};
