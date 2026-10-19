#pragma once
#include <chrono>
#include <memory>
#include <thread>
#include <mutex>
#include <zmq.hpp>
#include "config_parser.hpp"

class UserInteractionThread {
public:
    UserInteractionThread();
    ~UserInteractionThread();
    void start();
    void stop();

private:
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    zmq::socket_t socket_to_web_gateway{ctx, zmq::socket_type::push};

    std::thread worker_thread;
    std::atomic<bool> running{false};

    Config* config = Config::getInstance();

    void message_loop();
    void match_command(const std::string& message);
};
