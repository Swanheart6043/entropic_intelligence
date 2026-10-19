#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

struct Action {
    std::string action_id;
    std::string action_type;
    std::string action_value1;
    std::string action_value2;
    std::string action_value3;
    std::string action_value4;
    std::string action_remark;
    std::string action_wait;
};
struct ActionGroup {
    std::string action_group_id;
    std::vector<Action> action_list;
};

struct PatrolPoint {
    std::string point_name;
    std::string x;
    std::string y;
    std::string angle;
    std::string action_group_id;
};
struct PatrolRelations {
    std::string line_id;
    std::string patrol_speed;
    std::vector<PatrolPoint> point_list;
    std::string map_name;
};

enum class PlanType {
    PATROL, // 巡检
    DUTY // 值守
};
enum class ExecutionMode {
    ONCE, // 一次性执行
    REPEAT // 重复执行
};
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
    PlanType plan_type; // 计划类型
    int line_id; // 路线id
    ExecutionMode execution_mode; // 执行模式
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

class PatrolPlanHandler {
public:
    PatrolPlanHandler();
    ~PatrolPlanHandler();

    void save_action_group(WebSocketClient& websocket_connect, const std::string& message);
    void save_patrol_point(WebSocketClient& websocket_connect, const std::string& message);
    void save_patrol_plan(WebSocketClient& websocket_connect, const std::string& message);
    void save_duty_plan(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config_ = Config::getInstance();
    Config* config = Config::getInstance();

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_task_center_internal{ctx, zmq::socket_type::push};

    std::vector<ActionGroup> validate_and_format_action_group(const std::string& message);
    std::vector<PatrolRelations> validate_and_format_patrol_point(const std::string& message);
    std::vector<Plan> validate_and_format_patrol_plan(const std::string& message);
    std::vector<DutyPlan> validate_and_format_duty_plan(const std::string& message);
};