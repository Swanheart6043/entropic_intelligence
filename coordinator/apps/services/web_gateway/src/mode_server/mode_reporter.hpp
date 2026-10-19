#pragma once
#include <string>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class ModeReporter {
public:
    explicit ModeReporter(WebSocketClient& websocket_connect);
    ~ModeReporter();
    void start();    
    void stop();

private:
    Config* config = Config::getInstance();
    
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, ZMQ_SUB};
    
    std::string topic = "mode_info";
    
    WebSocketClient& websocket_connect_;
    
    std::thread report_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    
    std::atomic<bool> should_stop_{false};
    
    // 添加缓存，避免重复发送
    std::string last_mode = "";

    void report_loop();
};
