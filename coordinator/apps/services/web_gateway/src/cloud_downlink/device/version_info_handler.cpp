#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "version_info_handler.hpp"

VersionInfoHandler::VersionInfoHandler() {

}

VersionInfoHandler::~VersionInfoHandler() {

}

void VersionInfoHandler::handle(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        // 总结构
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_RobotVersion_Get"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Version", {
                {"PhoneixVersion", "0.0.0"},
                {"PlatanusVersion", "0.0.0"},
                {"GsoftwareVersion", "0.0.0"},
                {"McuVersion", "0.0.0"},
                {"APPVersion", "0.0.0"},
                {"PtzVersion", "V5.8.0 build 240806"}, // 写死，这个不会再变了
                {"PowVersion", "0.0.0"}
            }}
        };
        
        // 一次性读取 device_versions 表，构建 device_type -> sw_version 映射
        std::unordered_map<std::string, std::string> versions;
        {
            auto* conn = DBConnection::get_instance().get_connection();
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            std::unique_ptr<sql::ResultSet> res(
                stmt->executeQuery("SELECT device_type, sw_version FROM device_versions")
            );
            while (res->next()) {
                versions[res->getString("device_type")] = res->getString("sw_version");
            }
        }
        // 用查询结果填充存在的字段（查不到则保留默认值）
        auto fill = [&](const std::string& device_type, const char* json_key) {
            auto it = versions.find(device_type);
            if (it != versions.end()) {
                ack.at("Version").at(json_key) = it->second;
            } else {
                SPDLOG_WARN("device_versions 表中缺少 {} 的版本记录", device_type);
            }
        };
        fill("coordinator", "PhoneixVersion");
        fill("control", "McuVersion");
        fill("screen", "APPVersion");
        fill("perceiver", "GsoftwareVersion");
        fill("navigation", "PlatanusVersion");
        
        websocket_connect.send_message(ack.dump());
        SPDLOG_INFO("发送版本信息成功: {}", ack.dump());
    } catch (std::exception& e) {
        SPDLOG_ERROR("查询版本信息, 发生异常: {}", e.what());
    }
}