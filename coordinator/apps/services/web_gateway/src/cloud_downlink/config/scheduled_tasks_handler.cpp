#include <ctime>
#include <memory>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "scheduled_tasks_handler.hpp"

ScheduledTasksHandler::ScheduledTasksHandler() {

}

void ScheduledTasksHandler::handle(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_RobotEnableTask_Set"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1},
            {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        auto parsed = nlohmann::json::parse(message);
        auto switchs_json = parsed.at("Switchs");

        sql::Connection* con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);

        // 没有定时任务, 清空表后返回
        if (switchs_json.empty()) {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("DELETE FROM scheduled_tasks");
            con->commit();
            SPDLOG_INFO("保存定时任务成功, 清空数据");
            return;
        }

        // 校验并转换
        std::vector<Task> list = validate_and_format(switchs_json);
        if (list.empty()) {
            con->rollback();
            return;
        }

        // 先清空表，再写，全量
        {
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate("DELETE FROM scheduled_tasks");
        }

        // 批量插入
        std::string batch_sql = "INSERT INTO scheduled_tasks (task_type, task_value, start_time, end_time) VALUES ";
        for (size_t i = 0; i < list.size(); ++i) {
            if (i > 0) batch_sql += ", ";
            batch_sql += "(?, ?, ?, ?)";
        }

        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(batch_sql));
        int idx = 1;
        for (const auto& item : list) {
            pstmt->setString(idx++, item.type);
            pstmt->setString(idx++, item.value);
            pstmt->setString(idx++, item.start_time);
            pstmt->setString(idx++, item.end_time);
        }
        pstmt->executeUpdate();
        con->commit();
        SPDLOG_INFO("保存定时任务成功");
        
    } catch (sql::SQLException& e) {
        SPDLOG_ERROR("保存定时任务, 数据库错误: {}", e.what());
        try { DBConnection::get_instance().get_connection()->rollback(); } catch (...) {}
    } catch (std::exception& e) {
        SPDLOG_ERROR("保存定时任务, 发生异常: {}", e.what());
        try { DBConnection::get_instance().get_connection()->rollback(); } catch (...) {}
    }
}

std::vector<Task> ScheduledTasksHandler::validate_and_format(const nlohmann::json& switchs_json) {
    try {
        if (!switchs_json.is_array()) {
            SPDLOG_ERROR("Switchs 必须是数组");
            return {};
        }

        std::vector<Task> list;
        std::vector<std::string> errors;

        for (size_t i = 0; i < switchs_json.size(); ++i) {
            const auto& item = switchs_json[i];
            Task task;
            task.type = item.value("Type", "");
            task.value = item.value("Mode", "");
            std::string period = item.value("Period", "");

            if (task.type.empty()) {
                errors.push_back(fmt::format("Switchs[{}]: Type 不能为空", i));
            }
            if (task.value.empty()) {
                errors.push_back(fmt::format("Switchs[{}]: Mode 不能为空", i));
            }
            if (period.empty()) {
                errors.push_back(fmt::format("Switchs[{}]: Period 不能为空", i));
            } else {
                if (period.size() != 11 || period[5] != '-') {
                    errors.push_back(fmt::format("Switchs[{}]: Period 格式错误，应为 HH:MM-HH:MM", i));
                } else {
                    task.start_time = period.substr(0, 5);
                    task.end_time = period.substr(6, 5);
                    if (!is_valid_time(task.start_time) || !is_valid_time(task.end_time)) {
                        errors.push_back(fmt::format("Switchs[{}]: Period 时间无效，应为 00:00 至 23:59", i));
                    } else if (task.start_time > task.end_time) {
                        errors.push_back(fmt::format("Switchs[{}]: Period 不允许跨午夜，开始时间不能晚于结束时间", i));
                    }
                }
            }
            list.push_back(task);
        }
        if (!errors.empty()) {
            for (const auto& err : errors) {
                SPDLOG_ERROR("{}", err);
            }
            return {};
        }
        return list;
    } catch (std::exception& e) {
        SPDLOG_ERROR("解析定时任务异常: {}", e.what());
        return {};
    }
}

bool ScheduledTasksHandler::is_valid_time(const std::string& time) {
    if (time.size() != 5 || time[2] != ':') return false;
    for (const auto index : {0, 1, 3, 4}) {
        if (time[index] < '0' || time[index] > '9') return false;
    }
    const int hour = (time[0] - '0') * 10 + time[1] - '0';
    const int minute = (time[3] - '0') * 10 + time[4] - '0';
    return hour < 24 && minute < 60;
}
