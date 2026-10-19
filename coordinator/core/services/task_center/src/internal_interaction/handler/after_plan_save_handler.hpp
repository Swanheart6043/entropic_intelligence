#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "config_parser.hpp"
#include "robot_task/automatic/common/plan_common.hpp"

enum class ExecutionStatus {
    PENDING, // 待执行
    IN_PROGRESS, // 执行中
    COMPLETED, // 已完成
    PAUSED, // 已暂停
    CANCELED // 已取消
};
struct Plan {
    std::string plan_id;
    std::string plan_name; // 计划名
    plan::PlanType plan_type; // 计划类型
    int line_id; // 路线id
    plan::ExecutionMode execution_mode; // 执行模式
    std::string weekly; // 星期
    std::string start_time; // 计划开始时间
    std::string end_time; // 计划结束时间
    int gap; // 间隔时间
    bool is_finish_soon; // 是否在时间到后结束
};
struct DutyPlan {
    std::string duty_plan_id;
    std::string duty_plan_name;
    std::string start_time;
    std::string end_time;
    std::string weekly;
    int gap;
    std::string map_name;
    std::string point_name;
    int action_group_id;
};

class AfterUpdatePlanHandler {
public:
    explicit AfterUpdatePlanHandler();
    ~AfterUpdatePlanHandler();
    void handle_after_plan_save();
};
