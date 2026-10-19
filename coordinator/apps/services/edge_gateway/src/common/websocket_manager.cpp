#include <spdlog/spdlog.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "websocket_manager.hpp"

WebSocketManager& WebSocketManager::getInstance() {
    static WebSocketManager instance;
    return instance;
}

void WebSocketManager::start(uint16_t port) {
    if (m_running) {
        SPDLOG_WARN("WebSocket服务器已经在运行");
        return;
    }
    m_running = true;
    runServer(port);
}

void WebSocketManager::broadcast(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    
    if (m_connections.empty()) {
        SPDLOG_INFO("没有活跃的WebSocket连接，跳过广播");
        return;
    }
    
    for (auto const& hdl : m_connections) {
        try {
            m_server.send(hdl, msg, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("发送消息失败: {}", e.what());
        }
    }
}

size_t WebSocketManager::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    return m_connections.size();
}

void WebSocketManager::onOpen(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.insert(hdl);
    SPDLOG_INFO("客户端建立连接，当前连接数: {}", m_connections.size());
}

void WebSocketManager::onClose(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(m_connection_lock);
    m_connections.erase(hdl);
    SPDLOG_INFO("客户端断开连接，当前连接数: {}", m_connections.size());
}

void WebSocketManager::onMessage(connection_hdl hdl, server::message_ptr msg) {
    auto message_text = msg->get_payload();
    SPDLOG_INFO("收到消息: {}", message_text);
    if (m_message_callback) {
        m_message_callback(message_text);
    }
}

void WebSocketManager::runServer(uint16_t port) {
    try {
        // 清除所有访问日志通道（会移除 [connect]、[disconnect]、frame_header/payload 等）
        m_server.clear_access_channels(websocketpp::log::alevel::all);
        // 清除所有错误日志通道
        m_server.clear_error_channels(websocketpp::log::elevel::all);
        // 保险起见也清空内部 alog/elog 通道
        m_server.get_alog().clear_channels(websocketpp::log::alevel::all);
        m_server.get_elog().clear_channels(websocketpp::log::elevel::all);

        m_server.init_asio();
        m_server.set_reuse_addr(true);
        
        // 使用 lambda 绑定到成员函数
        m_server.set_open_handler([this](connection_hdl hdl) {
            this->onOpen(hdl);
        });
        
        m_server.set_close_handler([this](connection_hdl hdl) {
            this->onClose(hdl);
        });
        
        m_server.set_message_handler([this](connection_hdl hdl, server::message_ptr msg) {
            this->onMessage(hdl, msg);
        });
        
        m_server.listen(port);
        m_server.start_accept();
        
        SPDLOG_INFO("WebSocket服务器启动成功，监听端口: {}", port);
        m_server.run();

    } catch (const std::exception& e) {
        SPDLOG_ERROR("WebSocket服务器启动失败: {}", e.what());
        m_running = false;
    }
}

void WebSocketManager::setMessageCallback(MessageCallback callback) {
    m_message_callback = callback;
}
