#pragma once
#include <string>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class HardwareInfoHandler {
public:
    HardwareInfoHandler();
    void handle_hardware_info(WebSocketClient& websocket_connect);
    void handle_switch_info(WebSocketClient& websocket_connect, const std::string& message);

private: 
    Config* config = Config::getInstance();
};