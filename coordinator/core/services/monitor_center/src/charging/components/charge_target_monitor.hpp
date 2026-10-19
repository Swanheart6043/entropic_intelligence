#pragma once
#include <atomic>
#include <optional>
#include <string>
#include <thread>
#include <chrono>
#include <zmq.hpp>
#include "config_parser.hpp"

class ChargeTargetMonitor {
public:
    ChargeTargetMonitor();
    ~ChargeTargetMonitor();
    void start();
    void stop();

private:
    Config* config = Config::getInstance();
    std::atomic<bool> running{false};
    std::thread worker_thread;

    // 以下状态只在 worker_thread 内访问，无需加锁
    std::string charging_info;
    std::string power_info;
    std::optional<bool> last_is_charge = std::nullopt;   // 上一次充电状态
    bool charging_complete_notified = false;              // 已发送充电完成通知

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, ZMQ_SUB};
    zmq::socket_t socket_task_center{ctx, zmq::socket_type::push};
    zmq::socket_t socket_internal_command{ctx, zmq::socket_type::push};
    zmq::socket_t socket_web{ctx, zmq::socket_type::push};

    void run_loop();
    void handle_charging_state_change(bool is_charge);   // 原 action_loop 的动作
    void check_charge_target();                          // 原 target_loop 的检查
    void handle_enter_charging();
    void poll_and_handle_message();
    void handle_charging_info(const std::string& message);
};