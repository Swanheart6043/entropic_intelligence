#pragma once
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <thread>
#include <chrono>
#include "./websocket/client_ws.hpp"

using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using namespace std;

class WebSocketClient {
public:
    struct ReconnectConfig {
        bool enable_auto_reconnect;
        int max_reconnect_attempts;
        int initial_delay_ms;
        int max_delay_ms;
        double backoff_multiplier;

        ReconnectConfig() : enable_auto_reconnect(true), max_reconnect_attempts(-1), initial_delay_ms(1000), max_delay_ms(30000), backoff_multiplier(2.0) {}
    };

    // 心跳/探活配置：用于检测半开连接（断电、丢WiFi、NAT超时等对端无FIN/RST的场景）
    struct HeartbeatConfig {
        bool enable_heartbeat; // 是否启用协议层 ping/pong 看门狗
        int ping_interval_ms; // 发送 ping 的间隔
        int pong_timeout_ms; // 超过此时长未收到任何 pong/数据，判定半开连接并重连

        HeartbeatConfig() : enable_heartbeat(true), ping_interval_ms(10000), pong_timeout_ms(30000) {}
    };

    WebSocketClient(const std::string& server_address);
    WebSocketClient(const std::string& server_address, const ReconnectConfig& config);
    ~WebSocketClient();

    void start_client();
    void stop_client();
    bool send_message(const std::string& message);
    void set_message_callback(std::function<void(const std::string&)> callback);
    void set_connection_callback(std::function<void(bool)> callback);
    bool is_connected() const;

    // 手动触发重连
    void reconnect();

    // 配置重连参数
    void set_reconnect_config(const ReconnectConfig& config);

    // 配置心跳参数（需在 start_client 之前调用）
    void set_heartbeat_config(const HeartbeatConfig& config);

private:
    void create_client();
    void setup_callbacks();
    void internal_start();
    void schedule_reconnect();
    void reset_reconnect_state();

    // 心跳看门狗
    void start_heartbeat();
    void stop_heartbeat();
    void heartbeat_loop();
    void mark_alive();   // 收到任何对端数据/pong 时刷新存活时间

private:
    std::unique_ptr<WsClient> client_;
    std::string server_address_;

    // 连接状态
    mutable std::mutex connection_mutex_;
    std::condition_variable connection_cv_;
    std::shared_ptr<WsClient::Connection> connection_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> is_reconnecting_{false};

    // 重连相关
    ReconnectConfig reconnect_config_;
    std::atomic<int> reconnect_attempts_{0};
    std::atomic<int> current_delay_ms_{1000};
    std::thread reconnect_thread_;

    // 心跳/探活相关
    HeartbeatConfig heartbeat_config_;
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;
    // 最后一次确认对端存活的时间点（收到 pong 或任意消息时刷新）
    std::atomic<long long> last_alive_ms_{0};

    // 回调函数
    std::function<void(const std::string&)> message_callback_;
    std::function<void(bool)> connection_callback_;
};