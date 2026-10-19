#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "db_connection.hpp"
#include "startup_task_executor.hpp"

StartupTaskThread::StartupTaskThread() {

}

StartupTaskThread::~StartupTaskThread() {
    stop();
}

void StartupTaskThread::start() {
    if (running) {
        SPDLOG_WARN("启动任务线程已经在启动了");
        return;
    }
    running = true;
    coordinator_version_thread = std::thread(&StartupTaskThread::check_coordinator_version, this);
    navigation_version_thread = std::thread(&StartupTaskThread::check_navigation_version, this);
}

void StartupTaskThread::stop() {
    if (!running) {
        SPDLOG_WARN("自启动任务线程没有启动，无需停止");
        return;
    }
    running = false;
    if (coordinator_version_thread.joinable()) coordinator_version_thread.join();
    if (navigation_version_thread.joinable()) navigation_version_thread.join();
}

void StartupTaskThread::check_coordinator_version() {
    try {
        if (!need_save) return;

        auto* conn = DBConnection::get_instance().get_connection();
        std::string sql =
            "INSERT INTO device_versions (device_type, sw_version) VALUES (?, ?) "
            "ON DUPLICATE KEY UPDATE sw_version = VALUES(sw_version)";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        
        pstmt->setString(1, "coordinator");
        pstmt->setString(2, "CH266.27.C-H1-v2.0.0");
        pstmt->executeUpdate();

        pstmt->setString(1, "perceiver");
        pstmt->setString(2, "CH266.27.P-H1-v2.0.0");
        pstmt->executeUpdate();

        need_save = false;
        SPDLOG_INFO("保存工控版本号成功");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("保存工控版本号异常: {}", e.what());
    }
}

void StartupTaskThread::check_navigation_version() {
    try {
        auto http = httplib::Client{"http://10.10.10.10:8080"};
        http.set_connection_timeout(2, 0);
        http.set_read_timeout(2, 0);

        httplib::Result response = http.Get("/gs-robot/info");
        if (!response || response->status != 200) {
            SPDLOG_WARN("获取导航版本失败");
            return;
        } 
        nlohmann::json body = nlohmann::json::parse(response->body);
        auto version = body.at("data").at("softwareVersion").get<std::string>();
        
        auto* conn = DBConnection::get_instance().get_connection();
        std::string sql =
            "INSERT INTO device_versions (device_type, sw_version) VALUES (?, ?) "
            "ON DUPLICATE KEY UPDATE sw_version = VALUES(sw_version)";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        
        pstmt->setString(1, "navigation");
        pstmt->setString(2, version);
        pstmt->executeUpdate();

        SPDLOG_INFO("保存导航版本号成功");
    } catch (std::exception& e) {
        SPDLOG_WARN("保存导航版本号失败: {}", e.what());
    }
}