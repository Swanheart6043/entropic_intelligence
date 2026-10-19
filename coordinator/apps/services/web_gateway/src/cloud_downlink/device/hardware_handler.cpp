#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "hardware_handler.hpp"

HardwareInfoHandler::HardwareInfoHandler() {

}

void HardwareInfoHandler::handle_hardware_info(WebSocketClient& websocket_connect) {
    nlohmann::json base_info = {
        {"Header", {
            {"CmdType", "BaseInfoPost"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Info", {
            {"AudioOption", {{
                {"AudioCode", "g711a"}, 
                {"ChannelNum", 6}
            }}},
            {"CameraOption", {
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 4096}, 
                    {"VideoCode", "H264"}, 
                    {"DevType", "MINGJING"}, 
                    {"ChannelNum", 0}, 
                    {"BitRateType", 1}
                },
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 8192}, 
                    {"VideoCode", "H264"}, 
                    {"DevType", "HIKVISION-ONE"}, 
                    {"ChannelNum", 1}, 
                    {"BitRateType", 1}
                },
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 4096}, 
                    {"VideoCode", "H264"}, 
                    {"DevType", "ZICHUAN"}, 
                    {"ChannelNum", 2}, 
                    {"BitRateType", 1}
                },
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 4096}, 
                    {"VideoCode", "H265"}, 
                    {"DevType", "MINGJING"}, 
                    {"ChannelNum", 3}, 
                    {"BitRateType", 1}
                },
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 4096}, 
                    {"VideoCode", "H265"}, 
                    {"DevType", "MINGJING"}, 
                    {"ChannelNum", 4}, 
                    {"BitRateType", 1}
                },
                {
                    {"VideoType", 0}, 
                    {"StreamType", 1}, 
                    {"BitRateLimit", 4096}, 
                    {"VideoCode", "H265"}, 
                    {"DevType", "MINGJING"}, 
                    {"ChannelNum", 5}, 
                    {"BitRateType", 1}
                }
            }},
            {"Model", "H1"},
            {"Key", config->get_sn()},
            {"Sn", config->get_sn()},
            {"VoiceType", "xiaoyan"}
        }}
    };
    websocket_connect.send_message(base_info.dump());
    SPDLOG_INFO("基础信息发送成功: {}", base_info.dump());
}

void HardwareInfoHandler::handle_switch_info(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        using namespace nlohmann;

        sql::Connection* con = DBConnection::get_instance().get_connection();

        std::string sql = "SELECT config_key, config_value FROM settings_basic";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(sql));

        std::unordered_map<std::string, std::string> settings;
        while (res->next()) {
            settings[res->getString("config_key")] = res->getString("config_value");
        }

        json robot_switch = {
            {"Header", {
                {"CmdType", "RC_RobotEnable_Get"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Switchs", {
                {{"Type", "14"}, {"Value", settings["head_light"] == "on" ? "1" : "0"}},
                {{"Type", "12"}, {"Value", settings["warning_light"] == "on" ? "1" : "0"}},
                {{"Type", "23"}, {"Value", "0"}, {"Value2", settings["continuous_wiper"] == "on" ? "1" : "0"}},
                {{"Type", "5"}, {"Value", settings["volume"]}},
            }}
        };

        websocket_connect.send_message(robot_switch.dump());
        SPDLOG_INFO("使能信息: {}", robot_switch.dump());
    } catch (std::exception& e) {
        SPDLOG_ERROR("查询开关信息, 发生异常: {}", e.what());
    }
}