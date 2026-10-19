#include <sstream>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "db_connection.hpp"

DBConnection& DBConnection::get_instance() {
    static DBConnection instance;
    return instance;
}

std::unique_ptr<sql::Connection> DBConnection::create_connection() {
    auto* driver = sql::mysql::get_mysql_driver_instance();
    auto* config = Config::getInstance();
    
    std::unique_ptr<sql::Connection> conn(driver->connect(
        config->get_db_connection(),
        config->get_db_user_name(),
        config->get_db_password()
    ));
    conn->setSchema(config->get_schema());
    conn->setClientOption("characterSetResults", "utf8");
    
    std::ostringstream oss;
    oss << std::this_thread::get_id();
    SPDLOG_INFO("线程 {} 创建数据库连接", oss.str());
    return conn;
}

sql::Connection* DBConnection::get_connection() {
    // 关键：thread_local 让每个线程有独立的连接
    thread_local std::unique_ptr<sql::Connection> conn;
    
    // 检查连接是否需要重建
    bool need_reconnect = false;
    if (!conn) {
        need_reconnect = true;
    } else {
        try {
            if (conn->isClosed() || !conn->isValid()) {
                need_reconnect = true;
            }
        } catch (const sql::SQLException& e) {
            SPDLOG_WARN("连接检查异常，重建连接: {}", e.what());
            need_reconnect = true;
        }
    }
    if (need_reconnect) {
        try {
            conn = create_connection();
        } catch (const sql::SQLException& e) {
            SPDLOG_ERROR("数据库连接失败: {}", e.what());
            throw;
        }
    }
    return conn.get();
}
