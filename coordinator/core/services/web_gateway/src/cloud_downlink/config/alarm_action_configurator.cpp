#include <ctime>
#include <memory>
#include <stdexcept>
#include <limits>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "alarm_action_configurator.hpp"

namespace {
    // 兼容云端把数值传成字符串或 JSON number 的两种情况。
    std::string scalar_to_string(const nlohmann::json& object, const char* field_name) {
        if (!object.contains(field_name) || object.at(field_name).is_null()) {
            return {};
        }
        const auto& value = object.at(field_name);
        if (value.is_string()) {
            return value.get<std::string>();
        }
        if (value.is_number_integer() || value.is_number_unsigned() || value.is_number_float() || value.is_boolean()) {
            return value.dump();
        }
        return {};
    }
    bool has_value(const nlohmann::json& object, const char* field_name) {
        return !scalar_to_string(object, field_name).empty();
    }
}

void AlarmConfigHandler::handle(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    bool transaction_started = false;
    bool auto_commit_disabled = false;
    bool saved = false;
    std::string saved_alarm_type;

    try {
        const auto parsed = nlohmann::json::parse(message);
        const auto& robot_alarm_json = parsed.at("Robot_Alarm");
        const AlarmConfig alarm_config = validate_and_format(robot_alarm_json);

        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);
        auto_commit_disabled = true;
        transaction_started = true;

        // 新配置是该 AlarmType 的完整配置，先清理旧 ActionUnit。
        std::unique_ptr<sql::PreparedStatement> delete_stmt(con->prepareStatement("DELETE FROM alarm_action_config WHERE alarm_type = ?"));

        delete_stmt->setString(1, alarm_config.alarm_type);
        delete_stmt->executeUpdate();

        const std::string insert_sql = R"(
            INSERT INTO alarm_action_config (
                alarm_type, action_id, action_name, action_count,
                unit_order, action_type, action_value1, action_value2,
                action_wait_ms
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        )";

        std::unique_ptr<sql::PreparedStatement> insert_stmt(con->prepareStatement(insert_sql));
        for (const auto& unit : alarm_config.units) {
            insert_stmt->setString(1, alarm_config.alarm_type);
            insert_stmt->setString(2, alarm_config.action_id);
            insert_stmt->setString(3, alarm_config.action_name);
            insert_stmt->setInt(4, alarm_config.action_count);
            insert_stmt->setInt(5, unit.unit_order);
            insert_stmt->setString(6, unit.action_type);
            insert_stmt->setString(7, unit.action_value1);
            insert_stmt->setString(8, unit.action_value2);
            insert_stmt->setInt(9, unit.action_wait_ms);
            insert_stmt->executeUpdate();
        }

        con->commit();
        transaction_started = false;

        con->setAutoCommit(true);
        auto_commit_disabled = false;

        saved = true;
        saved_alarm_type = alarm_config.alarm_type;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("保存告警配置失败: {}", e.what());
        if (con != nullptr && transaction_started) {
            try {
                con->rollback();
            } catch (const std::exception& rollback_error) {
                SPDLOG_ERROR("告警配置回滚失败: {}", rollback_error.what());
            }
        }
        if (con != nullptr && auto_commit_disabled) {
            try {
                con->setAutoCommit(true);
            } catch (const std::exception& auto_commit_error) {
                SPDLOG_ERROR("恢复数据库自动提交失败: {}", auto_commit_error.what());
            }
        }
        return;
    }
    // ACK 与数据库事务分离，发送失败不会误判数据库写入失败。
    try {
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_Robot_DEAL_ALRAM"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());
        SPDLOG_INFO("告警配置保存成功: AlarmType={}", saved_alarm_type);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("告警配置已保存，但成功响应发送失败: {}", e.what());
    }
}

AlarmConfig AlarmConfigHandler::validate_and_format(
    const nlohmann::json& robot_alarm_json) {
    if (!robot_alarm_json.is_object()) {
        throw std::invalid_argument("Robot_Alarm 必须是对象");
    }
    AlarmConfig result;
    result.alarm_type = scalar_to_string(robot_alarm_json, "AlarmType");

    if (result.alarm_type.empty()) {
        throw std::invalid_argument("Robot_Alarm.AlarmType 不能为空");
    }
    if (!robot_alarm_json.contains("ActionList") || !robot_alarm_json.at("ActionList").is_array()) {
        throw std::invalid_argument("Robot_Alarm.ActionList 必须是数组");
    }
    const auto& action_list = robot_alarm_json.at("ActionList");
    if (action_list.size() != 1) {
        throw std::invalid_argument("Robot_Alarm.ActionList 必须且只能包含一个元素");
    }
    const auto& action = action_list.at(0);
    result.action_id = scalar_to_string(action, "ActionID");
    result.action_name = scalar_to_string(action, "ActionName");
    result.action_count = non_negative_int(action, "ActionCount");

    const auto& units = action.at("ActionUnit");

    for (size_t j = 0; j < units.size(); ++j) {
        const auto& unit_json = units.at(j);
        if (!unit_json.is_object() || !has_value(unit_json, "ActionType") || !has_value(unit_json, "ActionWait")) {
            throw std::invalid_argument("ActionList[0].ActionUnit[" + std::to_string(j) + "] 缺少必要字段");
        }

        AlarmActionUnit unit;
        unit.unit_order = static_cast<int>(j);
        unit.action_type = scalar_to_string(unit_json, "ActionType");
        unit.action_value1 = scalar_to_string(unit_json, "ActionValue1");
        unit.action_value2 = scalar_to_string(unit_json, "ActionValue2");
        unit.action_wait_ms = non_negative_int(unit_json, "ActionWait");
        result.units.push_back(std::move(unit));
    }
    if (result.action_count != static_cast<int>(result.units.size())) {
        SPDLOG_WARN(
            "AlarmType={} 的 ActionCount={}，但 ActionUnit 数量为 {}",
            result.alarm_type, result.action_count, result.units.size()
        );
    }
    return result;
}

int AlarmConfigHandler::non_negative_int(const nlohmann::json& object, const char* field_name) {
    const std::string text = scalar_to_string(object, field_name);

    try {
        size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);

        if (text.empty() || consumed != text.size() || value < 0 || value > std::numeric_limits<int>::max()) {
            throw std::invalid_argument("invalid number");
        }

        return static_cast<int>(value);
    } catch (...) {
        throw std::invalid_argument(
            std::string(field_name) + " 必须是非负整数");
    }
}