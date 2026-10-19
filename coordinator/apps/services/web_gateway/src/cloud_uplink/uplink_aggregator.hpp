#pragma once
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class CloudUplinkAggregator {
public:
    explicit CloudUplinkAggregator(WebSocketClient& websocket_connect);
    ~CloudUplinkAggregator();
    void start();    
    void stop();

private:
    Config* config = Config::getInstance();
    std::shared_ptr<spdlog::logger> logger;

    zmq::context_t ctx{1};
    zmq::socket_t socket_platform{ctx, zmq::socket_type::pull};

    WebSocketClient& websocket;
    std::thread worker_thread;

    void clear_old_logs(const std::string& dir, int max_day);
    void create_log(const std::string& path);
    void listen_processes();
};