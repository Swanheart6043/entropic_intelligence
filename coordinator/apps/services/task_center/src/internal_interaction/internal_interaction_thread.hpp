#pragma once
#include <thread>
#include <zmq.hpp>
#include "./handler/battery_too_hot_handler.hpp"
#include "./alarm_actions/alarm_actions_handler.hpp"

class InternalCommandThread {
public:
    InternalCommandThread();
    ~InternalCommandThread();
    void start();
    void stop();

private:    
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    zmq::socket_t socket_rep{ctx, zmq::socket_type::rep};

    std::atomic<bool> running{false};
    std::thread pull_thread;
    std::thread rep_thread;

    BatteryTooHotHandler battery_too_hot_handler;
    AlarmActionHandler alarm_action_handler;    

    void pull_loop();
    void match_message(const std::string& message);

    void rep_loop();
};
