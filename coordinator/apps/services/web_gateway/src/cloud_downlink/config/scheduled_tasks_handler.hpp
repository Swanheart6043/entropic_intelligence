#pragma once
#include <string>
#include <vector>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

struct Task {
    std::string type;
    std::string value;
    std::string start_time;
    std::string end_time;
};

class ScheduledTasksHandler {
public:
    ScheduledTasksHandler();
    void handle(WebSocketClient& websocket_connect, const std::string& message);
    
private:
    Config* config_ = Config::getInstance();
    static bool is_valid_time(const std::string& time);
    std::vector<Task> validate_and_format(const nlohmann::json& switchs_json);
};
