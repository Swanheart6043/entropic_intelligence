#pragma once
#include <string>
#include <chrono>
#include <atomic>
#include <functional>

enum class NavStatusCode {
    NAVIGATING = 405,
    ARRIVED = 407,
    SKIPPED = 404,
    TIMEOUT = 408,
    LOCALIZATION_LOST = 1006
};

enum class NavigationResult {
    SUCCESS,
    SKIPPED,
    TIMEOUT,
    CANCELLED,
    FAILED,
    LOCALIZATION_LOST,
    PAUSED
};

struct NavigationStatus {
    NavigationResult result;
    int code;
    std::string message;
};

struct NavigationRuntimeStatus {
    NavStatusCode status_code;
    std::string message;
    std::chrono::steady_clock::time_point last_update_time;
    int localization_lost_count = 0;

    bool is_progressing() const { 
        return status_code == NavStatusCode::NAVIGATING; 
    }
    bool is_abnormal() const { 
        return status_code == NavStatusCode::LOCALIZATION_LOST; 
    }
    bool is_terminal() const {
        return status_code == NavStatusCode::ARRIVED || status_code == NavStatusCode::SKIPPED || status_code == NavStatusCode::TIMEOUT;
    }
};

struct NavigationFinalResult {
    NavigationResult result;
    int status_code;
    std::string message;
};


// 导航目标（统一格式）
struct NavigationTarget {
    std::string map_name;
    double x = 0.0;
    double y = 0.0;
    double angle = 0.0;
    std::string point_name; // 可选，用于日志
};

// 导航任务类型
enum class NavigationTaskType {
    PATROL_POINT, // 巡逻点
    MANUAL_POINT, // 手动导航到点
    CHARGING, // 充电
    PATH_FOLLOWING // 跑线
};

// 导航控制上下文
struct NavigationControl {
    std::atomic<bool> should_cancel{false};
    std::atomic<bool> should_pause{false};
    
    void reset() {
        should_cancel.store(false);
        should_pause.store(false);
    }
};