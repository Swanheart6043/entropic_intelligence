#pragma once
#include <unordered_map>
#include <memory>
#include <atomic>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include "../common/task_run_outcome.hpp"
#include "duty_state_machine_common.hpp"
#include "duty_states.hpp"
#include "duty.hpp"

using namespace duty_task;

// ===================== 值守状态机 =====================
class DutyStateMachine {
public:
    using StatePtr = std::shared_ptr<DutyStateBase>;
    using TransitionMap = std::unordered_map<DutyEvent, DutyState>;

    static DutyStateMachine& instance();
    DutyStateMachine(const DutyStateMachine&) = delete;
    DutyStateMachine& operator=(const DutyStateMachine&) = delete;

    // ==================== 调度接口 ====================
    std::optional<DutyTask> query_duty_task();
    void init_duty_task(DutyTask task);
    TaskRunOutcome run_duty_task();
    bool resume_duty_snapshot();

    // ==================== 控制接口 ====================
    void cancel();
    void restart();

    // ==================== 设置接口 ====================
    void set_force_duty(bool force);

    // ==================== 查询接口 ====================
    DutyState get_current_state() const {return current_state;}
    std::string get_duty_uuid();

    // ==================== 判断接口 ====================
    bool is_running() const;
    bool is_force_duty() const;

private:
    std::atomic<bool> running{false};
    std::atomic<bool> force_duty{false};

    DutyState current_state;
    DutyState entry_state;
    DutyContext context;

    std::unordered_map<DutyState, TransitionMap> transitions;
    std::unordered_map<DutyState, StatePtr> states;
    std::unique_ptr<Duty> duty;

    DutyStateMachine();
    ~DutyStateMachine();

    void initialize_states();
    void initialize_transitions();
    void transition(DutyEvent event);
    void change_state(DutyState new_state);

    bool is_terminal(DutyState s) const;
};