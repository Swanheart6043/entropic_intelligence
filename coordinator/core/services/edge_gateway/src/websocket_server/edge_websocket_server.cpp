#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "common/websocket_manager.hpp"
#include "edge_websocket_server.hpp"
#include "./reporter/robot_info_reporter.hpp"

EdgeWebsocketServer::EdgeWebsocketServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx) 
    : robot_info_(robot_info), robot_info_mtx_(robot_info_mtx)
{
    
}

EdgeWebsocketServer::~EdgeWebsocketServer() {
    stop();
}

void EdgeWebsocketServer::start() {
    if (running) {
        SPDLOG_WARN("通用IPC交互线程, 已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&EdgeWebsocketServer::websocket_loop, this);
    state_thread = std::thread(&EdgeWebsocketServer::state_loop, this);
}

void EdgeWebsocketServer::stop() {
    
}

void EdgeWebsocketServer::websocket_loop() {
    WebSocketManager::getInstance().start(9002);
}

void EdgeWebsocketServer::state_loop() {
    report_robot_info(robot_info_, robot_info_mtx_);
}