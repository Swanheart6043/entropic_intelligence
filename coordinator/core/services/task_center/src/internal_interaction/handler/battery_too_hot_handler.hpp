#pragma once
#include <atomic>
#include <zmq.hpp>

class BatteryTooHotHandler {
public:
    BatteryTooHotHandler();
    static void handle_async();
private:
    void handle();
    inline static std::atomic<bool> running{false};
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};
};