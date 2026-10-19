#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <spdlog/spdlog.h>
#include "db_connection.hpp"
#include "db_settings.hpp"

std::string DBSettings::get(const std::string& key, const std::string& default_value) {
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("SELECT config_value FROM settings_basic WHERE config_key = ?")
        );
        pstmt->setString(1, key);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        if (res->next()) {
            return res->getString("config_value");
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("读取设置失败 [{}]: {}", key, e.what());
    }
    return default_value;
}

bool DBSettings::set(const std::string& key, const std::string& value) {
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> pstmt(
            conn->prepareStatement("UPDATE settings_basic SET config_value = ? WHERE config_key = ?")
        );
        pstmt->setString(1, value);
        pstmt->setString(2, key);
        pstmt->executeUpdate();
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("写入设置失败 [{}={}]: {}", key, value, e.what());
        return false;
    }
}

std::unordered_map<std::string, std::string> DBSettings::get_all() {
    std::unordered_map<std::string, std::string> result;
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(
            stmt->executeQuery("SELECT config_key, config_value FROM settings_basic")
        );
        while (res->next()) {
            result[res->getString("config_key")] = res->getString("config_value");
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("批量读取设置失败: {}", e.what());
    }
    return result;
}