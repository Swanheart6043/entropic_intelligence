#include <cstdint>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "threshold_config_handler.hpp"

namespace {
    int json_str_to_int(const nlohmann::json& j, const std::string& key) {
        return std::stoi(j.at(key).get<std::string>());
    }
}

ThresholdConfigHandler::ThresholdConfigHandler() {

}

void ThresholdConfigHandler::handle(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_RobotEnvConfig_Set"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1},
            {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());

        auto message_json = nlohmann::json::parse(message);
        ThresholdConfig tc = validate_and_format(message_json.at("EnvConfig"));

        auto conn = DBConnection::get_instance().get_connection();
        conn->setAutoCommit(false);
        {
            std::unique_ptr<sql::PreparedStatement>ps(conn->prepareStatement(
                "UPDATE battery_config SET "
                "battery_low_pct=?, battery_high_pct=?, charge_strategy=?, "
                "battery_temp_warn_c=?, battery_alarm_enabled=?, battery_temp_enabled=? "
                "WHERE id=1"
            ));
            ps->setInt(1, tc.power.battery_low_pct);
            ps->setInt(2, tc.power.battery_high_pct);
            ps->setString(3, tc.power.charge_strategy);
            ps->setInt(4, tc.power.battery_temp_warn_c);
            ps->setInt(5, tc.power.battery_alarm_enabled);
            ps->setInt(6, tc.power.battery_temp_enabled);
            ps->executeUpdate();
        }
        {
            std::unique_ptr<sql::PreparedStatement>ps(conn->prepareStatement(
                "UPDATE common_config SET "
                "fire_alarm_enabled=?, temp_alarm_enabled=?, humid_alarm_enabled=?, "
                "temp_top_x10=?, temp_low_x10=?, humid_top_x10=?, humid_low_x10=?, "
                "infrared_top_x10=? "
                "WHERE id=1"
            ));
            ps->setInt(1, tc.common.fire_alarm_enabled);
            ps->setInt(2, tc.common.temp_alarm_enabled);
            ps->setInt(3, tc.common.humid_alarm_enabled);
            ps->setInt(4, tc.common.temp_top_x10);
            ps->setInt(5, tc.common.temp_low_x10);
            ps->setInt(6, tc.common.humid_top_x10);
            ps->setInt(7, tc.common.humid_low_x10);
            ps->setInt(8, tc.common.infrared_top_x10);
            ps->executeUpdate();
        }
        conn->commit();
        conn->setAutoCommit(true);
        SPDLOG_INFO("阈值配置已保存");

    } catch (sql::SQLException& e) {
        SPDLOG_ERROR("保存配置, 数据库错误: {}", e.what());
        try { DBConnection::get_instance().get_connection()->rollback(); } catch (...) {}
    } catch (std::exception& e) {
        SPDLOG_ERROR("保存配置, 发生异常: {}", e.what());
        try { DBConnection::get_instance().get_connection()->rollback(); } catch (...) {}
    }
}

ThresholdConfig ThresholdConfigHandler::validate_and_format(const nlohmann::json& cfg) {
    ThresholdConfig out;

    // ---- power ----
    out.power.battery_low_pct       = json_str_to_int(cfg, "cPowerLowerLimit");
    out.power.battery_high_pct      = json_str_to_int(cfg, "cPowerTopLimit");
    int charge_flag                 = json_str_to_int(cfg, "iChargeEndPatrol"); // 0充电优先 1巡逻优先
    out.power.battery_temp_warn_c   = json_str_to_int(cfg, "betteryTopTemperature");
    out.power.battery_alarm_enabled = json_str_to_int(cfg, "cPowerEnable");
    out.power.battery_temp_enabled  = json_str_to_int(cfg, "betteryTempEnable");

    // ---- common ----
    out.common.fire_alarm_enabled  = json_str_to_int(cfg, "cInfraredTempEnable");
    out.common.temp_alarm_enabled  = json_str_to_int(cfg, "cTempEnable");
    out.common.humid_alarm_enabled = json_str_to_int(cfg, "cHumidityEnable");
    out.common.temp_top_x10        = json_str_to_int(cfg, "sTempTopLimit");
    out.common.temp_low_x10        = json_str_to_int(cfg, "sTempLowerLimit");
    out.common.humid_top_x10       = json_str_to_int(cfg, "sHumiditytopLimit");
    out.common.humid_low_x10       = json_str_to_int(cfg, "sHumidityLowerLimit");
    out.common.infrared_top_x10    = json_str_to_int(cfg, "sInfraredTempTopLimit");

    // ---- 校验 ----
    if (out.power.battery_low_pct < 0 || out.power.battery_high_pct > 100 ||
        out.power.battery_low_pct >= out.power.battery_high_pct) {
        throw std::invalid_argument("电量阈值非法");
    }
    if (out.common.temp_low_x10 >= out.common.temp_top_x10) {
        throw std::invalid_argument("温度阈值非法: low >= top");
    }
    if (out.common.humid_low_x10 >= out.common.humid_top_x10) {
        throw std::invalid_argument("湿度阈值非法: low >= top");
    }
    // iChargeEndPatrol(云端字段名有歧义,实为充电策略):0=充电优先 1=巡逻优先
    if (charge_flag == 0) {
        out.power.charge_strategy = "charge_first";
    } else if (charge_flag == 1) {
        out.power.charge_strategy = "patrol_first";
    } else {
        throw std::invalid_argument("iChargeEndPatrol 非 0/1: " + std::to_string(charge_flag));
    }
    // 开关位只接受 0/1(可选,防脏数据)
    auto check_bool = [](int v, const char* name) {
        if (v != 0 && v != 1) throw std::invalid_argument(std::string(name) + " 非 0/1");
    };
    check_bool(out.power.battery_alarm_enabled, "cPowerEnable");
    check_bool(out.power.battery_temp_enabled,  "betteryTempEnable");
    check_bool(out.common.fire_alarm_enabled,   "cInfraredTempEnable");
    check_bool(out.common.temp_alarm_enabled,   "cTempEnable");
    check_bool(out.common.humid_alarm_enabled,  "cHumidityEnable");

    return out;
}