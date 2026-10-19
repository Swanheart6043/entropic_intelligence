#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class OnlineReporter {
public:
    explicit OnlineReporter(WebSocketClient& websocket_connect);
    ~OnlineReporter();
    void start();    
    void stop();

private:
    static constexpr int INITIAL_RETRY_INTERVAL = 30; // 初始30秒
    static constexpr int MAX_RETRY_INTERVAL = 600; // 最大10分钟

    Config* config = Config::getInstance();
    WebSocketClient& websocket_connect_;
    std::thread report_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> is_online_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<int> retry_count_{0};

    void report_loop();
    bool try_report_once();
    bool check_network_connectivity();
    bool send_online();
    void send_heartbeat();
};
