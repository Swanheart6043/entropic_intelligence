#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include "robot_task/automatic/auto_task_scheduler.hpp"
#include "mode/mode_types.hpp"
#include "robot_task/task_ownership.hpp"

// 全局任务所有权协调器：同一时刻只允许自动任务、充电任务、手动运动之一占用机器人。
class RobotTaskCoordinator : public ModeObserver, public ManualMotionOwnership {
public:
    static RobotTaskCoordinator& instance();
    RobotTaskCoordinator(const RobotTaskCoordinator&) = delete;
    RobotTaskCoordinator& operator=(const RobotTaskCoordinator&) = delete;

    void start();
    void stop();

    void change_mode(RobotMode new_mode, std::optional<AutoModeOperation> operation) override;

    // 只取消自动计划；计划更新、巡逻暂停使用此接口，不影响充电/手动任务。
    void cancel_auto_task();

    // 全局运动取消；模式切换、SOS 等安全事件使用此接口。
    void cancel();
    void recover();
    void restart();
    void forget();

    void request_charging(ChargingReason reason);
    void notify_charging_complete();

    // 返回 false 表示充电执行器正在对桩，本次计划更新按业务规则不处理。
    bool handle_plan_updated();

    // ManualMotionExecutor 的任务边界协议。
    bool prepare_manual_motion() override;
    bool acquire_manual_motion(
        const std::atomic<bool>& requester_running,
        const std::atomic<bool>& requester_cancelled) override;
    void release_manual_motion() override;

    // 电池过热退桩等内部运动使用同一所有权协议。
    void prepare_internal_motion();
    bool acquire_internal_motion(const std::atomic<bool>& requester_running);
    void release_internal_motion();

    RobotTaskState current_state() const override;
    std::string get_task_uuid();
    bool is_idle() const;
    bool is_progress() const;

private:
    RobotTaskCoordinator();
    ~RobotTaskCoordinator();

    std::atomic<bool> running_{false};
    std::atomic<RobotMode> mode_{RobotMode::AUTO};
    std::atomic<RobotTaskState> state_{RobotTaskState::IDLE};
    std::thread worker_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    bool resume_requested_ = false;
    bool restart_requested_ = false;
    bool forget_requested_ = false;
    bool charging_complete_ = false;
    bool charging_cancel_requested_ = false;
    bool charging_should_restart_auto_ = false;
    std::optional<ChargingReason> charging_request_;

    AutoTaskScheduler auto_scheduler_;

    void worker_loop();
    bool run_pending_charging();
    AutoTaskRunResult run_auto_once(bool resume_snapshot);
    void handle_auto_result(const AutoTaskRunResult& result);
    void set_state(RobotTaskState state);
    void cancel_charging_session();
    void wait_while_manual();
    void sleeping(std::chrono::milliseconds duration);
};
