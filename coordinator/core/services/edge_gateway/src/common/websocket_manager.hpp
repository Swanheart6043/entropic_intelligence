#pragma once
#include <set>
#include <string>
#include <mutex>
#include <memory>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;

class WebSocketManager {
public:
    using MessageCallback = std::function<void(const std::string&)>;

    static WebSocketManager& getInstance();
    // 启动服务器（在单独线程中运行）
    void start(uint16_t port);
    // 广播消息给所有客户端
    void broadcast(const std::string& msg);
    // 获取当前连接数
    size_t getConnectionCount() const;
    void setMessageCallback(MessageCallback callback);

private:
    WebSocketManager() = default;
    ~WebSocketManager() = default;
    WebSocketManager(const WebSocketManager&) = delete;

    WebSocketManager& operator=(const WebSocketManager&) = delete;
    
    void onOpen(connection_hdl hdl);
    void onClose(connection_hdl hdl);
    void onMessage(connection_hdl hdl, server::message_ptr msg);
    void runServer(uint16_t port);
    
    server m_server;
    std::set<connection_hdl, std::owner_less<connection_hdl>> m_connections;
    mutable std::mutex m_connection_lock;
    bool m_running = false;
    MessageCallback m_message_callback;
};