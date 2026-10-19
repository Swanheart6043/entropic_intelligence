#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <mutex>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "httplib.hpp"
#include "config_parser.hpp"

class MapOperateHandler {
public:
    MapOperateHandler();
    void create_map(WebSocketClient& websocket_connect, const std::string& message);
    void remove_map(WebSocketClient& websocket_connect, const std::string& message);
    void create_point(WebSocketClient& websocket_connect, const std::string& message);
    void remove_point(WebSocketClient& websocket_connect, const std::string& message);
    void synchronous_base_info(WebSocketClient& websocket_connect);
    void synchronous_map_png(WebSocketClient& websocket_connect, const std::string& message);
    void synchronous_virtual_obstacles(const std::string& message);
    void synchronous_point(WebSocketClient& websocket_connect, const std::string& message);
    void read(WebSocketClient& websocket_connect);
    void remove(WebSocketClient& websocket_connect, const std::string& message);
    // 下载地图标注数据
    void download_map_labeling(WebSocketClient& websocket_connect, const std::string& message);
    // 上传地图标注数据
    void upload_map_labeling(WebSocketClient& websocket_connect, const std::string& message);

private:
    Config* config_ = Config::getInstance();
    Config* config = Config::getInstance();

    std::string base_url = "/gs-robot";
    std::mutex http_mutex_;
    httplib::Client http_{"http://10.10.10.10:8080"};
    std::unique_ptr<httplib::Client> nav_http;
    std::unique_ptr<httplib::Client> web_http;

    void send_ack(WebSocketClient& websocket_connect, const std::string& cmd_type);
};