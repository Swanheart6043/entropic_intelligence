#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "./websocket/client_ws.hpp"
#include "./websocket/client_wss.hpp"

class WebSocketClient {
public:
    class Transport;

    struct ReconnectConfig {
        bool enable_auto_reconnect{true};
        int max_reconnect_attempts{-1};
        int initial_delay_ms{1000};
        int max_delay_ms{30000};
        double backoff_multiplier{2.0};
    };

    struct HeartbeatConfig {
        bool enable_heartbeat{true};
        int ping_interval_ms{10000};
        int pong_timeout_ms{30000};
    };

    struct TlsConfig {
        bool verify_certificate{true};
        std::string ca_cert_file;
        std::string client_cert_file;
        std::string client_key_file;
    };

    explicit WebSocketClient(const std::string& server_address);
    WebSocketClient(const std::string& server_address, const ReconnectConfig& config);
    WebSocketClient(const std::string& server_address, const TlsConfig& tls_config);
    WebSocketClient(
        const std::string& server_address,
        const ReconnectConfig& config,
        const TlsConfig& tls_config
    );
    ~WebSocketClient();

    void start_client();
    void stop_client();
    // Asynchronous submission, safe to call from a downlink callback.
    bool send_message(const std::string& message);
    // Wait for the local socket write only; the cloud protocol has no event ACK.
    // Worker threads only: never block the WebSocket I/O callback thread.
    bool send_message_and_wait(const std::string& message, int timeout_ms = 5000);
    void set_message_callback(std::function<void(const std::string&)> callback);
    void set_connection_callback(std::function<void(bool)> callback);
    bool is_connected() const;

    void reconnect();
    void set_reconnect_config(const ReconnectConfig& config);
    void set_heartbeat_config(const HeartbeatConfig& config);

private:
    void create_client();
    void connection_loop();
    void request_reconnect(bool manual = false);
    void reset_reconnect_state();
    bool send_async(const std::string& message, std::function<void(bool)> completion,
                    std::shared_ptr<Transport>* used_transport = nullptr);

    void transport_opened();
    void transport_message(const std::string& message);
    void transport_closed(int status, const std::string& reason);
    void transport_error(const std::string& message);

    void start_heartbeat();
    void stop_heartbeat();
    void heartbeat_loop();
    void mark_alive();

    std::shared_ptr<Transport> transport_;
    std::string server_address_;
    ReconnectConfig reconnect_config_;
    TlsConfig tls_config_;

    mutable std::mutex connection_mutex_;
    std::condition_variable connection_cv_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> is_reconnecting_{false};

    std::atomic<int> reconnect_attempts_{0};
    std::atomic<int> current_delay_ms_{1000};
    std::thread client_thread_;
    std::thread reconnect_thread_;
    std::mutex reconnect_mutex_;
    std::condition_variable reconnect_cv_;
    bool reconnect_requested_{false};
    bool manual_reconnect_{false};

    HeartbeatConfig heartbeat_config_;
    std::thread heartbeat_thread_;
    std::atomic<bool> heartbeat_running_{false};
    std::mutex heartbeat_mutex_;
    std::condition_variable heartbeat_cv_;
    std::atomic<long long> last_alive_ms_{0};

    std::function<void(const std::string&)> message_callback_;
    std::function<void(bool)> connection_callback_;
};
