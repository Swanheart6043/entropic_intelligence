#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "version_handler.hpp"

VersionHandler::VersionHandler() {

}

VersionHandler::~VersionHandler() {

}

void VersionHandler::save(const std::string& version) {
    try {
        if (!need_save) return;

        auto* conn = DBConnection::get_instance().get_connection();
        std::string sql =
            "INSERT INTO device_versions (device_type, sw_version) VALUES (?, ?) "
            "ON DUPLICATE KEY UPDATE sw_version = VALUES(sw_version)";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        
        pstmt->setString(1, "control");
        pstmt->setString(2, version);
        pstmt->executeUpdate();

        need_save = false;
        SPDLOG_INFO("保存主控版本成功: {}", version);
    } catch (std::exception& e) {
        SPDLOG_ERROR("保存主控版本异常: {}", e.what());
    }
}