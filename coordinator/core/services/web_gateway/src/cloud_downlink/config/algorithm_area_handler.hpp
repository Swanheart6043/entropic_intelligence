#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class AlgorithmAreasHandler {
public: 
    AlgorithmAreasHandler();
    ~AlgorithmAreasHandler();
    void upload_algorithm_areas(WebSocketClient& websocket_connect, const std::string& message);
    void download_algorithm_areas(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config = Config::getInstance();
};