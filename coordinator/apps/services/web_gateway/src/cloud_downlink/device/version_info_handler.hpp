#pragma once
#include <string>
#include "common/wss_connector.hpp"
#include "httplib.hpp"
#include "config_parser.hpp"

class VersionInfoHandler {
public: 
    VersionInfoHandler();
    ~VersionInfoHandler();
    void handle(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config_ = Config::getInstance();
    httplib::Client http_{"http://10.10.10.10:8080"};
};