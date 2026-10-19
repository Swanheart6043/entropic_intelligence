#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <zmq.hpp>
#include "mode_types.hpp"

class ModeQueryServer {
public:
    using ModeProvider = std::function<RobotMode()>;

    ModeQueryServer(zmq::context_t& context, ModeProvider mode_provider);
    ~ModeQueryServer();

    void start();
    void stop();

private:
    zmq::socket_t socket_;
    ModeProvider mode_provider_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    void message_loop();
};
