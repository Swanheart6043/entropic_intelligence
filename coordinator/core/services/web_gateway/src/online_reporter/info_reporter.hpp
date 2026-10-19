#pragma once
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class InfoReporter {
public:
    explicit InfoReporter(WebSocketClient& websocket_connect);
    ~InfoReporter();
    void start();    
    void stop();

private:
    Config* config = Config::getInstance();
    WebSocketClient& websocket_connect_;
    std::thread report_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> should_stop_{false};

    void report_loop();
};
