#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "screenshot_handler.hpp"

ScreenshotHandler::ScreenshotHandler() {
    int linger = 1000;
	int sndtimeo = 1000;
    int sndhwm = 100;
    socket_to_task_center.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_task_center.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_task_center.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_task_center.connect("tcp://127.0.0.1:8223");
}

void ScreenshotHandler::handle_message(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 发送回执
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "Call_SOS_hangUp"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        // 校验消息
        auto alarm_info = nlohmann::json::parse(message).at("AlarmRecord");
        auto alarm_type = alarm_info.value("alarmType", 0);
        if (alarm_type <= 0) {
            SPDLOG_WARN("人工截图触发告警时，告警类型不能为空");
            return;
        }
        // 执行联动动作组
        send_alarm_actions(alarm_type);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("通知边缘网关异常: {}", e.what());
    }
}

void ScreenshotHandler::send_alarm_actions(int code) {
    std::vector<AlarmAction> actions;
    try {
        actions = query_alarm_actions(code);
    } catch (const sql::SQLException& e) {
        SPDLOG_WARN("查询告警 {} 的处置动作失败: {}", code, e.what());
        return;
    }
    if (actions.empty()) {
        SPDLOG_WARN("告警 {} 未配置处置动作，跳过", code);
        return;
    }
    
    nlohmann::json message = {{"command", "alarm-actions"}, {"action_list", actions}};
    socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
}

std::vector<AlarmAction> ScreenshotHandler::query_alarm_actions(int code) {
    std::vector<AlarmAction> actions;
    sql::Connection* con = DBConnection::get_instance().get_connection();

    std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(R"(
        SELECT action_type, action_value1, action_value2, action_wait_ms
        FROM alarm_action_config
        WHERE alarm_type = ?
        ORDER BY unit_order ASC
    )"));
    stmt->setString(1, std::to_string(code));
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
    
    while (rs->next()) {
        actions.push_back({
            rs->getString("action_type"), 
            rs->getString("action_value1"), 
            rs->getString("action_value2"), 
            rs->getInt("action_wait_ms")
        });
    }
    return actions;
}