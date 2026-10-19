#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include "component/navigate_executor/navigation_types.hpp"

struct DutyTask {
    int plan_id;
    std::string plan_name;
    std::string weekly;
    std::string start_time;
    std::string end_time;
    int gap;
    std::string map_name;
    std::string point_name;
    int action_group_id;
};

enum class DutyState {
    IDLE,
    QUERY_PLAN,
    CHECK_CONDITION,
    PREPARE_PATROL,
    NAVIGATING,
    EXECUTING_ACTION,
    WAITING_GAP,
    REPORTING,
    PAUSED,
    ERROR,
    COMPLETED,
    CANCELLED
};

enum class DutyEvent {
    START,
    PLAN_FOUND,
    NO_PLAN,
    CHECK_PASSED,
    CHECK_FAILED,
    PREPARATION_DONE,
    NAVIGATION_SUCCESS,
    NAVIGATION_FAILED,
    NAVIGATION_TIMEOUT,
    ACTION_COMPLETE,
    ACTION_FAILED,
    GAP_ELAPSED,
    END_TIME_REACHED,
    ALL_COMPLETE,
    PAUSE_REQUEST,
    RESUME_REQUEST,
    CANCEL_REQUEST,
    ERROR_OCCURRED,
};

struct DutyContext {
    DutyTask duty_info;
    NavigationStatus last_nav_status;
    std::string error_message;
    std::string duty_report_uuid;
    std::atomic<bool> should_cancel{false};
};