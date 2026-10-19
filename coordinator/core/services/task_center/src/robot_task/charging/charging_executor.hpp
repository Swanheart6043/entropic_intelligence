#pragma once
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <zmq.hpp>
#include "httplib.hpp"
#include "robot_task/task_types.hpp"
#include "docking_controller.hpp"

enum class ChargingOutcome {
    DOCKED,
    CANCELLED,
    FAILED,
};

enum class ChargingPhase {
    IDLE,
    PREPARING,
    NAVIGATING,
    DOCKING,
    DOCKED,
};

// 充电业务执行器。线程与全局任务所有权由 RobotTaskCoordinator 管理。
class ChargingExecutor {
public:
    static ChargingExecutor& instance() {
        static ChargingExecutor executor;
        return executor;
    }
    ChargingExecutor(const ChargingExecutor&) = delete;
    ChargingExecutor& operator=(const ChargingExecutor&) = delete;
    
    ChargingOutcome run(ChargingReason reason);
    // 由 RobotTaskCoordinator 在授予 RUNNING_CHARGING 所有权前调用，
    // 防止“刚取消、run 又清掉取消标志”的启动竞态。
    void prepare_run();
    void cancel_charging();
    void finish_session();

    void leave_charge_station();

    void set_charge_type();
    void get_charge_type();

    bool is_active() const;
    bool is_going_to_charge() const;
    bool is_docking() const;
    ChargingPhase phase() const;

private:
    static constexpr int MAX_NAV_RETRY = 99; // 导航到充电点最大重试次数
    static constexpr int NAV_RETRY_DELAY_SECONDS = 5; // 导航到充电点重试间隔

    std::unique_ptr<DockingController> docking_controller;

    std::atomic<bool> executing{false};
    std::atomic<bool> should_cancel{false};
    std::atomic<ChargingPhase> phase_{ChargingPhase::IDLE};

    std::unique_ptr<httplib::Client> http;
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_web_gateway{ctx, zmq::socket_type::push};
    
    std::string current_map = "";
    std::string point_name = "";
    std::string charge_type = "";

    ChargingExecutor();
    ~ChargingExecutor();

    bool find_charge_point();
    bool nav_to_charge_point();
    bool interruptible_wait(std::chrono::milliseconds duration) const;
    bool check_conditions_for_entering_hibernation();
    void enter_hibernation();
};
