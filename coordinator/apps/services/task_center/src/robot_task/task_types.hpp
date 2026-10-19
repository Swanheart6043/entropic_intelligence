#pragma once

enum class RobotTaskState {
    IDLE,
    RUNNING_AUTO_TASK,
    RUNNING_CHARGING,
    RUNNING_MANUAL_MOTION,
    RUNNING_INTERNAL_MOTION,
};

enum class ChargingReason {
    LOW_POWER,
    PATROL_COMPLETED,
    DUTY_COMPLETED,
    HUMAN_REQUEST,
};

inline const char* charging_reason_name(ChargingReason reason) {
    switch (reason) {
        case ChargingReason::LOW_POWER: return "低电量";
        case ChargingReason::PATROL_COMPLETED: return "巡逻完成";
        case ChargingReason::DUTY_COMPLETED: return "值守完成";
        case ChargingReason::HUMAN_REQUEST: return "人工操作";
    }
    return "未知原因";
}
