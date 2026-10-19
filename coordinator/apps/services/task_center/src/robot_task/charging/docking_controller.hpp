#pragma once
#include <zmq.hpp>
#include <atomic>
#include <string>
#include <functional>

enum class DockingResult {
    SUCCESS,
    TIMEOUT,
    CANCELLED,
    ERROR
};

class DockingController {
public:
    DockingController(zmq::context_t& ctx);
    DockingResult start(std::function<bool()> navigate_func, std::atomic<bool>& cancel_flag);

private:
    static constexpr int MAX_RETRY = 10;
    static constexpr int TIMEOUT_SECONDS = 120;
    static constexpr int POLL_INTERVAL_MS = 100;

    zmq::context_t& ctx_; // 外部传入，必须比本对象活得久
    zmq::socket_t socket_to_mcu; // PUSH，常驻
    zmq::socket_t socket_from_mcu; // SUB，每次对桩前重建以丢弃历史积压

    void reset_sub_socket(); // 重建全新 SUB，避免读到陈旧状态
    void send_message();
    DockingResult wait(std::atomic<bool>& cancel_flag);
    void slightly_lift_tire_toward_rear();
};