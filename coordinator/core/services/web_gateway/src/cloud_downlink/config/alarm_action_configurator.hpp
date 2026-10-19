#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

struct AlarmActionUnit {
    int unit_order{};
    std::string action_type;
    std::string action_value1;
    std::string action_value2;
    int action_wait_ms{};
};

struct AlarmConfig {
    std::string alarm_type;
    std::string action_id;
    std::string action_name;
    int action_count{};
    std::vector<AlarmActionUnit> units;

    bool empty() const noexcept {
        return alarm_type.empty() || action_id.empty() || units.empty();
    }
};

class AlarmConfigHandler {
public:
    AlarmConfigHandler() = default;
    void handle(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config = Config::getInstance();

    AlarmConfig validate_and_format(const nlohmann::json& robot_alarm_json);
    int non_negative_int(const nlohmann::json& object, const char* field_name);
};