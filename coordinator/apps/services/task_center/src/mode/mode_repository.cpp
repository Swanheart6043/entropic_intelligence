#include "mode_repository.hpp"
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <spdlog/spdlog.h>
#include "db_connection.hpp"

RobotMode ModeRepository::load_mode() {
    constexpr int kMaxRetries = 5;
    constexpr int kBaseDelayMs = 500;

    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
        try {
            sql::Connection* connection = DBConnection::get_instance().get_connection();
            std::unique_ptr<sql::Statement> statement(connection->createStatement());
            std::unique_ptr<sql::ResultSet> result(statement->executeQuery(
                "SELECT config_value FROM settings_basic WHERE config_key = 'mode'"));

            if (result->next()) {
                const std::string mode = result->getString("config_value");
                SPDLOG_INFO("从数据库加载模式: {}", mode);
                return mode == "auto" ? RobotMode::AUTO : RobotMode::MANUAL;
            }

            SPDLOG_WARN("数据库中未找到mode配置,使用默认AUTO模式");
            return RobotMode::AUTO;
        } catch (sql::SQLException& e) {
            SPDLOG_WARN(
                "初始化读取模式失败 (尝试 {}/{}): {} (错误码: {})",
                attempt + 1,
                kMaxRetries,
                e.what(),
                e.getErrorCode());
            if (attempt + 1 < kMaxRetries) {
                const int delay = kBaseDelayMs * (1 << attempt);
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }
        }
    }
    SPDLOG_ERROR("初始化读取模式: 重试 {} 次仍失败, 进程退出等待重启", kMaxRetries);
    throw std::runtime_error("ModeRepository 初始化失败: 无法连接数据库读取模式");
}

void ModeRepository::save_mode(RobotMode mode) {
    try {
        sql::Connection* connection = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement("UPDATE settings_basic SET config_value = ? WHERE config_key = 'mode'"));
        statement->setString(1, mode == RobotMode::AUTO ? "auto" : "manual");
        statement->executeUpdate();

        SPDLOG_INFO("模式已保存到数据库: {}", mode == RobotMode::AUTO ? "auto" : "manual");
    } catch (sql::SQLException& e) {
        SPDLOG_ERROR("保存模式到数据库失败: {} (错误码: {})", e.what(), e.getErrorCode());
    }
}
