#include <ctime>
#include <memory>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "magic_enum/magic_enum.hpp"
#include "db_connection.hpp"
#include "plan_handler.hpp"

PatrolPlanHandler::PatrolPlanHandler() {
    int linger = 1000;
	int sndtimeo = 1000;
    int sndhwm = 100;
    socket_to_task_center_internal.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_task_center_internal.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_task_center_internal.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_task_center_internal.connect("tcp://127.0.0.1:8223");
}

PatrolPlanHandler::~PatrolPlanHandler() {

}

void PatrolPlanHandler::save_action_group(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    try {
        using namespace nlohmann;
        json ack = {
            {"Header", {
                {"CmdType", "RC_RobotAction_Set"}, 
                {"Sn", config_->get_sn()}, 
                {"Time", std::time(nullptr)}
            }}, 
            {"Result", 1}, {"Data", ""}
        };
        // 如果没有动作组
        if (json::parse(message).at("ActionList").empty()) {
            websocket_connect.send_message(ack.dump());
            return;
        }
        // 如果有动作组, 判断参数对不对
        auto action_group = validate_and_format_action_group(message);
        if (action_group.empty()) {
            SPDLOG_ERROR("参数错误");
            return;
        }
        websocket_connect.send_message(ack.dump());

        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);
        // 清空动作表
        {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("DELETE FROM plans_actions");
        }
        // 构建SQL
        int total_actions = 0;
        for (const auto& group : action_group) {
            total_actions += group.action_list.size();
        }
        if (total_actions > 0) {
            std::string batchSQL = "INSERT INTO plans_actions (action_group_id, action_id, action_type, action_value1, action_value2, action_value3, action_value4, action_remark, action_wait) VALUES ";

            for (int i = 0; i < total_actions; ++i) {
                if (i > 0) batchSQL += ", ";
                batchSQL += "(?, ?, ?, ?, ?, ?, ?, ?, ?)";
            }

            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(batchSQL));

            // 设置所有参数
            int paramIndex = 1;
            for (const auto& group : action_group) {
                for (const auto& action : group.action_list) {
                    pstmt->setInt(paramIndex++, std::stoi(group.action_group_id));
                    pstmt->setInt(paramIndex++, std::stoi(action.action_id));
                    pstmt->setString(paramIndex++, action.action_type);
                    pstmt->setString(paramIndex++, action.action_value1);
                    pstmt->setString(paramIndex++, action.action_value2);
                    pstmt->setString(paramIndex++, action.action_value3);
                    pstmt->setString(paramIndex++, action.action_value4);
                    pstmt->setString(paramIndex++, action.action_remark);
                    pstmt->setString(paramIndex++, action.action_wait);
                }
            }
            // 执行批量插入
            pstmt->executeUpdate();
        }
        con->commit();
        con->setAutoCommit(true);
        SPDLOG_INFO("保存动作组成功");
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("保存动作组, 数据库错误: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("保存动作组, 发生异常: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    }
}

void PatrolPlanHandler::save_patrol_point(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    try {
        using namespace nlohmann;

        auto point_after_format = validate_and_format_patrol_point(message);
        if (point_after_format.empty()) return;
        json ack = {
            {"Header", {
                {"CmdType", "NB_RobotPatrol_Laser_Set"},
                {"Sn", config_->get_sn()}, {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);
        // 清空点位表
        {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("DELETE FROM plans_points");
        }
        // 构建SQL
        int total_point = 0;
        for (const auto& group : point_after_format) {
            total_point += group.point_list.size();
        }
        if (total_point > 0) {
            std::string pointSQL = "INSERT INTO plans_points (line_id, point_type, patrol_speed, map_name, point_name, x, y, angle, action_group_id) VALUES ";
            for (int i = 0; i < total_point; ++i) {
                if (i > 0) pointSQL += ", ";
                pointSQL += "(?, ?, ?, ?, ?, ?, ?, ?, ?)";
            }
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(pointSQL));
            // 设置点位参数
            int paramIndex = 1;
            for (const auto& patrol : point_after_format) {
                for (const auto& point : patrol.point_list) {
                    pstmt->setInt(paramIndex++, std::stoi(patrol.line_id));
                    pstmt->setInt(paramIndex++, 2);
                    pstmt->setInt(paramIndex++, std::stoi(patrol.patrol_speed));
                    pstmt->setString(paramIndex++, patrol.map_name);
                    pstmt->setString(paramIndex++, point.point_name);
                    pstmt->setDouble(paramIndex++, point.x.empty() ? 0.0 : std::stod(point.x));
                    pstmt->setDouble(paramIndex++, point.y.empty() ? 0.0 : std::stod(point.y));
                    pstmt->setDouble(paramIndex++, point.angle.empty() ? 0.0 : std::stod(point.angle));
                    pstmt->setInt(paramIndex++, point.action_group_id.empty() ? 0 : std::stoi(point.action_group_id));
                }
            }
            // 执行批量插入
            pstmt->executeUpdate();
        }
        con->commit();
        con->setAutoCommit(true);
        SPDLOG_INFO("保存点位成功");
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("数据库错误: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("发生异常: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    }
}

void PatrolPlanHandler::save_patrol_plan(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    try {
        using namespace nlohmann;

        // 发送回执
        json ack = {
            {"Header", {
                {"CmdType", "NB_RobotPatrolPlan_Set"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 创建数据库连接
        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);

        // 判断参数
        auto plan_after_format = validate_and_format_patrol_plan(message);
        if (plan_after_format.empty()) {
            // 如果没有计划，清空表后返回
            {
                std::unique_ptr<sql::Statement> stmt(con->createStatement());
                stmt->executeUpdate("DELETE FROM plans");
            }
            con->commit();
            con->setAutoCommit(true);
            SPDLOG_INFO("保存巡逻计划成功, 清空数据");
            return;
        }

        // 全量更新 - 清空现有计划数据
        size_t total_plans = plan_after_format.size();
        {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("TRUNCATE TABLE plans");
        }
        // 构建SQL
        if (total_plans > 0) {
            std::string planSQL = R"(INSERT INTO plans (plan_id, plan_name, plan_type, execution_mode, line_id, start_time, end_time, weekly, gap, is_finish_soon, execution_status) VALUES )";                
            for (size_t i = 0; i < total_plans; ++i) {
                if (i > 0) planSQL += ", ";
                planSQL += "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            }
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(planSQL));
            // 设置计划参数
            int paramIndex = 1;
            for (const auto& plan : plan_after_format) {
                pstmt->setString(paramIndex++, plan.plan_id);
                pstmt->setString(paramIndex++, plan.plan_name);
                pstmt->setString(paramIndex++, magic_enum::enum_name(PlanType::PATROL).data());
                pstmt->setString(paramIndex++, magic_enum::enum_name(plan.execution_mode).data());
                pstmt->setInt(paramIndex++, plan.line_id);
                pstmt->setString(paramIndex++, plan.start_time);
                pstmt->setString(paramIndex++, plan.end_time);
                pstmt->setString(paramIndex++, plan.weekly);
                pstmt->setInt(paramIndex++, plan.gap);
                pstmt->setBoolean(paramIndex++, plan.is_finish_soon);
                pstmt->setString(paramIndex++, magic_enum::enum_name(ExecutionStatus::PENDING).data());
            }
            pstmt->executeUpdate();
        }
        con->commit();
        con->setAutoCommit(true);
        SPDLOG_INFO("保存巡逻计划成功");

        // 发送给任务中心
        nlohmann::json params = {{"command", "plan_has_been_updated"}};
        socket_to_task_center_internal.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("通知任务中心巡逻计划下发: {}", params.dump());
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("保存计划, 数据库错误: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("保存计划, 发生异常: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    }
}

void PatrolPlanHandler::save_duty_plan(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    try {
        using namespace nlohmann;

        // 发送回执
        json ack = {
            {"Header", {
                {"CmdType", "NB_RobotTaskPlan_Set"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 创建数据库连接
        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);

        // 判断参数
        auto plan_after_format = validate_and_format_duty_plan(message);
        if (plan_after_format.empty()) {
            // 如果没有计划，清空表后返回
            {
                std::unique_ptr<sql::Statement> stmt(con->createStatement());
                stmt->executeUpdate("DELETE FROM plans_duty");
            }
            con->commit();
            con->setAutoCommit(true);
            SPDLOG_INFO("保存值守计划成功, 清空数据");
            return;
        }

        // 清空现有计划数据（全量更新）
        size_t total_plans = plan_after_format.size();
        {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("TRUNCATE TABLE plans_duty");
        }
        // 构建SQL
        if (total_plans > 0) {
            std::string sql = R"(INSERT INTO plans_duty (duty_plan_id, duty_plan_name, start_time, end_time, weekly, gap, map_name, point_name, action_group_id, execution_status) VALUES )";                
            for (size_t i = 0; i < total_plans; ++i) {
                if (i > 0) sql += ", ";
                sql += "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
            }
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
            // 设置计划参数
            int paramIndex = 1;
            for (const auto& plan : plan_after_format) {
                pstmt->setString(paramIndex++, plan.duty_plan_id);
                pstmt->setString(paramIndex++, plan.duty_plan_name);
                pstmt->setString(paramIndex++, plan.start_time);
                pstmt->setString(paramIndex++, plan.end_time);
                pstmt->setString(paramIndex++, plan.weekly);
                pstmt->setInt(paramIndex++, plan.gap);
                pstmt->setString(paramIndex++, plan.map_name);
                pstmt->setString(paramIndex++, plan.point_name);
                pstmt->setInt(paramIndex++, plan.action_group_id);
                pstmt->setString(paramIndex++, magic_enum::enum_name(ExecutionStatus::PENDING).data());
            }
            pstmt->executeUpdate();
        }
        con->commit();
        con->setAutoCommit(true);
        SPDLOG_INFO("保存值守计划成功");

        // 发送给任务中心
        nlohmann::json params = {{"command", "plan_has_been_updated"}};
        socket_to_task_center_internal.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("通知任务中心值守计划下发: {}", params.dump());
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("保存值守计划, 数据库错误: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("保存值守计划, 发生异常: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    }
}

std::vector<ActionGroup> PatrolPlanHandler::validate_and_format_action_group(const std::string& message) {
    try {
        using namespace nlohmann;
        nlohmann::json action_group_list = json::parse(message).at("ActionList");
       
        if (!action_group_list.is_array()) {
            SPDLOG_ERROR("有动作组的, ActionList必须是个数组");
            return std::vector<ActionGroup>{};
        }
        std::vector<ActionGroup> group_list;
        std::vector<Action> action_list;

        for (const auto &item : action_group_list) {
            ActionGroup group;
            auto group_id = item.value("ActionID", "");
            auto action_unit_list = item.value("ActionUnitList", json::array());

            if (group_id.empty()) {
                SPDLOG_ERROR("ActionID不能为空");
                return std::vector<ActionGroup>{};
            }

            if (action_unit_list.empty()) {
                SPDLOG_ERROR("ActionUnitList不能为空");
                return std::vector<ActionGroup>{};
            }

            group.action_group_id = group_id;
            for (const auto &sub_item : action_unit_list) {
                Action action;
                auto action_id = sub_item.value("ActionUnitID", "");
                auto action_type = sub_item.value("ActionType", "");
                auto action_remark = sub_item.value("ActionUnitName", "");
                auto action_value1 = sub_item.value("ActionValue1", "");
                auto action_value2 = sub_item.value("ActionValue2", "");
                auto action_value3 = sub_item.value("ActionValue3", "");
                auto action_value4 = sub_item.value("ActionValue4", "");
                auto action_wait = sub_item.value("ActionWait", "");
                if (action_type.empty()) {
                    SPDLOG_ERROR("ActionType不能为空");
                    return std::vector<ActionGroup>{};
                }
                action.action_id = action_id;
                action.action_type = action_type;
                action.action_value1 = action_value1;
                action.action_value2 = action_value2;
                action.action_value3 = action_value3;
                action.action_value4 = action_value4;
                action.action_remark = action_remark;
                action.action_wait = action_wait;
                action_list.push_back(std::move(action));
            }
            group.action_list = std::move(action_list);
            group_list.push_back(std::move(group));
        }
        SPDLOG_INFO("解析和格式化动作组参数成功!");
        return group_list;
    } catch (std::exception &e) {
        SPDLOG_ERROR("解析和格式化下发计划 - 动作组, 发生异常: ");
        return std::vector<ActionGroup>{};
    }
}

std::vector<PatrolRelations> PatrolPlanHandler::validate_and_format_patrol_point(const std::string& message) {
    std::vector<PatrolRelations> patrol_list;
    try {
        using namespace nlohmann;
        
        nlohmann::json patrol_info_raw = json::parse(message).at("PatrolList").value("PatrolInfo", json::array());
        
        if (patrol_info_raw.empty()) {
            SPDLOG_ERROR("解析和格式化保存点位时, PatrolInfo不能为空");
            return patrol_list;
        }
        
        for (const auto &item : patrol_info_raw) {
            PatrolRelations patrol;
            std::vector<PatrolPoint> point_list;
            auto line_id = item.value("PatrolID", "");
            auto patrol_speed = item.value("PatrolSpeed", "");
            auto map_name = item.value("MapName", "");
            auto point = item.value("Point", json::array());

            if (line_id.empty()) {
                SPDLOG_ERROR("PatrolID不能为空");
                return std::vector<PatrolRelations>{};
            }
            
            if (patrol_speed.empty()) {
                SPDLOG_ERROR("PatrolSpeed不能为空");
                return std::vector<PatrolRelations>{};
            }
            
            if (map_name.empty()) {
                SPDLOG_ERROR("MapName不能为空");
                return std::vector<PatrolRelations>{};
            }
            if (point.empty()) {
                SPDLOG_ERROR("Point不能为空");
                return std::vector<PatrolRelations>{};
            }

            patrol.line_id = line_id;
            patrol.patrol_speed = patrol_speed;
            patrol.map_name = map_name;
            for (const auto &sub_item : point) {
                PatrolPoint patrol_point; 
                auto name = sub_item.value("PointID", "");
                auto x = sub_item.value("PointX", "");
                auto y = sub_item.value("PointY", "");
                auto angle = sub_item.value("Angle", "");
                auto action_list = sub_item.value("ActionList", json::array());

                if (name.empty()) {
                    SPDLOG_ERROR("PointID不能为空");
                    return std::vector<PatrolRelations>{};
                }

                if (x.empty()) {
                    SPDLOG_ERROR("PointX不能为空");
                    return std::vector<PatrolRelations>{};
                }

                if (y.empty()) {
                    SPDLOG_ERROR("PointY不能为空");
                    return std::vector<PatrolRelations>{};
                }

                patrol_point.point_name = name;
                patrol_point.x = x;
                patrol_point.y = y;
                patrol_point.angle = angle;
                if (!action_list.empty() && action_list[0].is_object()) {
                    patrol_point.action_group_id = std::to_string(action_list[0].value("ActionID", 0));
                }
                point_list.push_back(std::move(patrol_point));
            }
            patrol.point_list = std::move(point_list);
            patrol_list.push_back(std::move(patrol));
        }
        
        SPDLOG_INFO("解析和格式化保存点位成功!");
        return patrol_list;

    } catch (std::exception &e) {
        SPDLOG_ERROR("解析和格式化下发计划 - 点位, 发生异常: {}", e.what());
        return std::vector<PatrolRelations>{};
    }
}

std::vector<Plan> PatrolPlanHandler::validate_and_format_patrol_plan(const std::string& message) {
    try {
        using namespace nlohmann;
        
        nlohmann::json received = json::parse(message).value("PatrolPlanList", json::array());
        if (received.empty()) return std::vector<Plan>{};
        
        std::vector<Plan> plan_list;
        for (const auto &item : received) {
            Plan plan;
            
            // 计划id
            if (!item.contains("ucPatrolPlanId") || !item["ucPatrolPlanId"].is_string()) {
                SPDLOG_ERROR("下发计划时, ucPatrolPlanId不能为空");
                return std::vector<Plan>{};    
            }
            plan.plan_id = item.at("ucPatrolPlanId");

            // 计划名称
            if (!item.contains("unPlanName") || !item["unPlanName"].is_string()) {
                SPDLOG_ERROR("下发计划时, unPlanName不能为空");
                return std::vector<Plan>{};    
            }
            plan.plan_name = item.at("unPlanName");
            
            // 模式
            if (!item.contains("unSingleExecution") || !item["unSingleExecution"].is_string()) {
                SPDLOG_ERROR("下发计划时, unSingleExecution不能为空");
                return std::vector<Plan>{};    
            }
            plan.execution_mode = item.at("unSingleExecution") == "0" ? ExecutionMode::REPEAT : ExecutionMode::ONCE;
            
            // 开始时间
            if (!item.contains("ucStartTime") || !item["ucStartTime"].is_string()) {
                SPDLOG_ERROR("下发计划时, ucStartTime不能为空");
                return std::vector<Plan>{};    
            }
            plan.start_time = item.at("ucStartTime");
            
            // 星期
            if (!item.contains("unWeeklyPlan") || !item["unWeeklyPlan"].is_string()) {
                SPDLOG_ERROR("下发计划时, unWeeklyPlan不能为空");
                return std::vector<Plan>{};    
            }
            plan.weekly = item.at("unWeeklyPlan");
            
            // 路线
            if (!item.contains("ucPatrolID") || !item["ucPatrolID"].is_string()) {
                SPDLOG_ERROR("下发计划时, ucPatrolID不能为空");
                return std::vector<Plan>{};    
            }
            plan.line_id = std::stoi(item.at("ucPatrolID").get<std::string>());
            
            // 结束时间
            if ((!item.contains("ucEndTime") || !item["ucEndTime"].is_string()) && plan.execution_mode == ExecutionMode::REPEAT) {
                SPDLOG_ERROR("下发计划时, 循环模式下ucEndTime不能为空");
                return std::vector<Plan>{};    
            }
            plan.end_time = item.at("ucEndTime");
            
            // 间隔
            if ((!item.contains("unInterval") || !item["unInterval"].is_string()) && plan.execution_mode == ExecutionMode::REPEAT) {
                std::cout << std::endl << "下发计划时, 循环模式下unInterval不能为空" << std::endl;
                return std::vector<Plan>{};
            }
            plan.gap = std::stoi(item.at("unInterval").get<std::string>());
            
            // 到时间是否立即停止
            if ((!item.contains("interactiveSupport") || !item["interactiveSupport"].is_string()) && plan.execution_mode == ExecutionMode::REPEAT) {
                SPDLOG_ERROR("下发计划时, 循环模式下interactiveSupport不能为空");
                return std::vector<Plan>{};
            }
            plan.is_finish_soon = item.at("interactiveSupport") == "1" ? true : false;
            plan_list.push_back(std::move(plan));
        }
        SPDLOG_INFO("下发计划-计划, 解析参数成功! ");
        return plan_list;
    } catch (std::exception &e) {
        SPDLOG_ERROR("下发计划-计划, 解析参数发生异常: {}", e.what());
        return std::vector<Plan>{};
    }
}

std::vector<DutyPlan> PatrolPlanHandler::validate_and_format_duty_plan(const std::string& message) {
    try {
        using namespace nlohmann;
        
        nlohmann::json received = json::parse(message).value("TaskPlanList", json::array());
        if (received.empty()) return std::vector<DutyPlan>{};
        
        std::vector<DutyPlan> plan_list;
        for (const auto &item : received) {
            DutyPlan plan;
            // 计划id
            if (!item.contains("TaskID") || !item["TaskID"].is_string()) {
                SPDLOG_ERROR("下发计划时, TaskID不能为空");
                return std::vector<DutyPlan>{};    
            }
            plan.duty_plan_id = item.at("TaskID");

            // 计划名称
            if (!item.contains("planName") || !item["planName"].is_string()) {
                SPDLOG_ERROR("下发计划时, planName不能为空");
                return std::vector<DutyPlan>{};    
            }
            plan.duty_plan_name= item.at("planName");
            
            // 开始时间
            if (!item.contains("ucStartTime") || !item["ucStartTime"].is_string()) {
                SPDLOG_ERROR("下发计划时, ucStartTime不能为空");
                return std::vector<DutyPlan>{};    
            }
            plan.start_time = item.at("ucStartTime");
            
            // 结束时间
            if ((!item.contains("ucEndTime") || !item["ucEndTime"].is_string())) {
                SPDLOG_ERROR("下发计划时, 循环模式下ucEndTime不能为空");
                return std::vector<DutyPlan>{};    
            }
            plan.end_time = item.at("ucEndTime");

            // 星期
            if (!item.contains("unWeeklyPlan") || !item["unWeeklyPlan"].is_string()) {
                SPDLOG_WARN("下发计划时, unWeeklyPlan不能为空");
                return std::vector<DutyPlan>{};    
            }
            plan.weekly = item.at("unWeeklyPlan");
            
            // 间隔
            if ((!item.contains("unInterval") || !item["unInterval"].is_string())) {
                SPDLOG_WARN("下发计划时, unInterval不能为空");
                return std::vector<DutyPlan>{};
            }
            plan.gap = std::stoi(item.at("unInterval").get<std::string>());

            // 地图名
            if ((!item.contains("MapName") || !item["MapName"].is_string())) {
                SPDLOG_WARN("下发计划时, MapName不能为空");
                return std::vector<DutyPlan>{};
            }
            plan.map_name = item.at("MapName").get<std::string>();
            
            // 点位名
            if ((!item.contains("nValue") || !item["nValue"].is_string())) {
                SPDLOG_WARN("下发计划时, nValue不能为空");
                return std::vector<DutyPlan>{};
            }
            plan.point_name = item.at("nValue").get<std::string>();
            
            // 动作组id
            if ((!item.contains("ActionList") || !item["ActionList"].is_array())) {
                SPDLOG_WARN("下发计划时, ActionList不能为空");
                return std::vector<DutyPlan>{};
            }
            plan.action_group_id = std::stoi(item.at("ActionList")[0].at("ActionID").get<std::string>());
            
            plan_list.push_back(std::move(plan));
        }
        SPDLOG_INFO("下发计划-计划, 解析参数成功! ");
        return plan_list;
    } catch (std::exception &e) {
        SPDLOG_ERROR("下发计划-计划, 解析参数发生异常: {}", e.what());
        return std::vector<DutyPlan>{};
    }
}