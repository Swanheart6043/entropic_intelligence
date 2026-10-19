#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include "robot_task/automatic/common/plan_common.hpp"
#include "component/navigate_executor/navigation_types.hpp"

// ==================== 状态定义 ====================
enum class PatrolState {
    IDLE,
    QUERY_PLAN,
    CHECK_CONDITION,
    PREPARE_PATROL,
    START_CYCLE,
    NAVIGATING,
    EXECUTING_ACTION,
    END_CYCLE,
    WAITING_GAP,
    REPORTING,
    CANCELLED,
    COMPLETED,
    ERROR,
};

enum class PatrolEvent {
    START,
    PLAN_FOUND,
    NO_PLAN,
    CHECK_PASSED,
    CHECK_FAILED,
    PREPARATION_DONE,
    CYCLE_STARTED,
    NAVIGATION_SUCCESS,
    NAVIGATION_SKIPPED,
    NAVIGATION_FAILED,
    NAVIGATION_TIMEOUT,
    ACTION_COMPLETE,
    ACTION_FAILED,
    CYCLE_ENDED,
    GAP_ELAPSED,
    END_TIME_REACHED,
    CYCLE_COMPLETE,
    ALL_COMPLETE,
    PAUSE_REQUEST,
    RESUME_REQUEST,
    CANCEL_REQUEST,
    ERROR_OCCURRED,
    RETRY
};

// ==================== 任务数据 ====================
struct PatrolTaskPoint {
    std::string map_name;
    std::string point_name;
    int line_id;
    std::string x;
    std::string y;
    std::string angle;
    int action_group_id;
    std::string patrol_speed;
};

struct PatrolTask {
    int plan_id;
    std::string plan_name;
    plan::PlanType plan_type;
    int line_id;
    plan::ExecutionMode execution_mode;
    std::string weekly;
    std::string start_time;
    std::string end_time;
    int gap;
    bool is_finish_soon;
    std::vector<PatrolTaskPoint> point_list;
};

// ==================== 状态机上下文 ====================
struct PatrolContext {
    PatrolTask patrol_info;
    size_t current_point_index = 0;
    int cycle_count = 1;
    int point_count = 0;
    int history_point_count = 0;
    std::string patrol_report_uuid;
    NavigationStatus last_nav_status;
    std::string error_message;
    std::atomic<bool> should_cancel{false};
    std::atomic<bool> save_snapshot_on_cancel{false};
    
    void reset_for_next_cycle() {
        current_point_index = 0;
        point_count = 0;
        cycle_count++;
    }
    bool has_more_points() const {
        return current_point_index < patrol_info.point_list.size();
    }
    const PatrolTaskPoint& get_current_point() const {
        return patrol_info.point_list[current_point_index];
    }
    void move_to_next_point() {
        current_point_index++;
        point_count++;
        history_point_count++;
    }
};
