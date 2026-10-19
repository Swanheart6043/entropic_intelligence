#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <spdlog/spdlog.h>
#include "db_connection.hpp"
#include "battery_config_dao.hpp"

PowerConfig PowerConfigDao::get() {
    PowerConfig cfg{
        20, 80, "charge_first", 60, 1, 1 // 默认值,和建表 DEFAULT 对齐
    };

    try {
        auto* conn = DBConnection::get_instance().get_connection();

        std::unique_ptr<sql::PreparedStatement>ps(conn->prepareStatement(
            "SELECT battery_low_pct, battery_high_pct, charge_strategy, "
            "battery_temp_warn_c, battery_alarm_enabled, battery_temp_enabled "
            "FROM battery_config WHERE id = 1"
        ));

        std::unique_ptr<sql::ResultSet> res(ps->executeQuery());
        if (res->next()) {
            cfg.battery_low_pct = res->getInt("battery_low_pct");
            cfg.battery_high_pct = res->getInt("battery_high_pct");
            cfg.charge_strategy = res->getString("charge_strategy");
            cfg.battery_temp_warn_c = res->getInt("battery_temp_warn_c");
            cfg.battery_alarm_enabled = res->getInt("battery_alarm_enabled");
            cfg.battery_temp_enabled = res->getInt("battery_temp_enabled");
        } else {
            SPDLOG_WARN("battery_config 无 id=1 行,返回默认值");
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("读取 battery_config 失败: {}", e.what());
        // 兜底返回默认值,调用方拿到的是安全可用的配置
    }

    return cfg;
}