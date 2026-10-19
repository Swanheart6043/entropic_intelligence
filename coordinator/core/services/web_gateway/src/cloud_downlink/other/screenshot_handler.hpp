#pragma once
#include <iostream>
#include <string>
#include <zmq.hpp>
#include <thread>
#include <chrono>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

struct AlarmAction {
    std::string action_type;
    std::string value1;
    std::string value2;
    int wait_ms;
};

inline void to_json(nlohmann::json& j, const AlarmAction& a) {
    j = nlohmann::json{
        {"action_type", a.action_type},
        {"value1", a.value1},
        {"value2", a.value2},
        {"wait_ms", a.wait_ms}
    };
}

inline void from_json(const nlohmann::json& j, AlarmAction& a) {
    a.action_type = j.value("action_type", "");
    a.value1 = j.value("value1", "");
    a.value2 = j.value("value2", "");
    a.wait_ms = j.value("wait_ms", 0);
}

class ScreenshotHandler {
public: 
    ScreenshotHandler();
    void handle_message(WebSocketClient& websocket_connect, const std::string& message);
    
private:
    Config* config_ = Config::getInstance();

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_task_center{ctx, zmq::socket_type::push};

    void send_alarm_actions(int code);
    std::vector<AlarmAction> query_alarm_actions(int code);
};