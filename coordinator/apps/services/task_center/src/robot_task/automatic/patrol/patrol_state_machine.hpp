#pragma once
#include <unordered_map>
#include <memory>
#include <atomic>
#include <optional>
#include <spdlog/spdlog.h>
#include "../common/task_run_outcome.hpp"
#include "patrol_state_machine_common.hpp"

class PatrolStateBase;
class Patrol;

// 巡逻快照: 切手动模式打断巡逻时保存, 切回自动+继续时恢复
struct PatrolSnapshot {
    PatrolTask patrol_info;
    size_t current_point_index;
    int cycle_count;
    std::string patrol_report_uuid;
    std::string throughout_broadcaster_text;
    int throughout_broadcaster_gap;
};

// ===================== 巡逻状态机 =====================
class PatrolStateMachine {
public:
    using StatePtr = std::shared_ptr<PatrolStateBase>;
    using TransitionMap = std::unordered_map<PatrolEvent, PatrolState>;

    static PatrolStateMachine& instance();
    PatrolStateMachine(const PatrolStateMachine&) = delete;
    PatrolStateMachine& operator=(const PatrolStateMachine&) = delete;

    // ==================== 调度接口 ====================
    std::optional<PatrolTask> query_patrol_task();
    void init_patrol_task(PatrolTask task);
    TaskRunOutcome run_patrol_task();
    bool resume_patrol_snapshot();
    
    // ==================== 控制接口 ====================
    void request_cancel(bool save_snapshot);
    void restart();

    // ==================== 设置接口 ====================
    void set_resume_snapshot_pending();
    void set_force_patrol(bool force);

    // ==================== 查询接口 ====================
    PatrolState get_current_state() const;
    std::string get_patrol_uuid();

    // ==================== 判断接口 ====================
    bool is_running() const;
    bool is_snapshot();
    bool is_cancel() const;
    bool is_force_patrol() const;

private:
    std::atomic<bool> running{false};
    std::atomic<bool> force_patrol{false};
    std::atomic<bool> resume_snapshot_pending_{false};

    PatrolState current_state;
    PatrolState state_before_pause;
    PatrolState entry_state_; // try_acquire_task 决定: 正常 CHECK_CONDITION, 快照恢复 NAVIGATING
    PatrolContext context;

    std::unordered_map<PatrolState, TransitionMap> transitions_;
    std::unordered_map<PatrolState, StatePtr> states_;
    std::unique_ptr<Patrol> patrol;    

    // 快照
    std::optional<PatrolSnapshot> context_snapshot;
    PatrolState state_snapshot;

    PatrolStateMachine();
    ~PatrolStateMachine();

    void initialize_states();
    void initialize_transitions();
    
    void transition(PatrolEvent event);
    void change_state(PatrolState new_state);
    void handle_cancel_request();
    void snapshot();
    void resume();

    bool is_terminal(PatrolState s) const;
};