#pragma once
#include <string>
#include <cstdint>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "battery_config.hpp"

struct CommonConfig {
    int fire_alarm_enabled; // 烟火(红外高温)开关 0/1  <- cInfraredTempEnable
    int temp_alarm_enabled; // 温度告警开关 0/1
    int humid_alarm_enabled; // 湿度告警开关 0/1
    int temp_top_x10; // 温度上限 ×10
    int temp_low_x10; // 温度下限 ×10
    int humid_top_x10; // 湿度上限 ×10
    int humid_low_x10; // 湿度下限 ×10
    int infrared_top_x10; // 红外温度上限 ×10
};

struct ThresholdConfig {
    PowerConfig  power;
    CommonConfig common;
};

class ThresholdConfigHandler {
public: 
    ThresholdConfigHandler();
    void handle(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config_ = Config::getInstance();
    ThresholdConfig validate_and_format(const nlohmann::json& switchs_json);
};