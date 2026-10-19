#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class HighFreedomNavigator {
public:
    explicit HighFreedomNavigator(WebSocketClient& websocket);
    ~HighFreedomNavigator();
    HighFreedomNavigator(const HighFreedomNavigator&) = delete;
    HighFreedomNavigator& operator=(const HighFreedomNavigator&) = delete;

    // 由 websocket 接收线程调用,必须立即返回、绝不阻塞
    void submit(const std::string& message);

private:
    // 一条通道:队列 + 锁 + 条件变量 + worker 线程 + http 客户端
    struct Channel {
        std::deque<std::string> queue;
        std::mutex mutex;
        std::condition_variable cv;
        std::thread worker;
        httplib::Client http;

        explicit Channel(const std::string& host, int port) : http(host, port) {}
    };

    void worker_loop(Channel& ch);
    void process(Channel& ch, const std::string& message);
    void reply(const nlohmann::json& uuid, nlohmann::json result = nullptr);
    nlohmann::json parse_body(const std::string& body);

    bool is_slow_url(const std::string& url) const;

    WebSocketClient& websocket_;
    std::mutex send_mutex_;   // 保护 websocket_.send_message 的并发调用

    Channel fast_;
    Channel slow_;
    bool running_ = true;
    static constexpr size_t kMaxQueue = 30;

    Config* config_ = Config::getInstance();
};