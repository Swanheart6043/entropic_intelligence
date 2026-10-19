#include "websocket_client.hpp"
#include <iostream>
#include <algorithm>
#include <spdlog/spdlog.h>

namespace {
    // 当前 steady_clock 毫秒
    long long now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
}

WebSocketClient::WebSocketClient(const std::string& server_address)
    : server_address_(server_address), reconnect_config_() {
    current_delay_ms_ = reconnect_config_.initial_delay_ms;
    create_client();
}

WebSocketClient::WebSocketClient(const std::string& server_address, const ReconnectConfig& config)
    : server_address_(server_address), reconnect_config_(config) {
    current_delay_ms_ = config.initial_delay_ms;
    create_client();
}

WebSocketClient::~WebSocketClient() {
    stop_client();
}

void WebSocketClient::create_client() {
    client_ = std::make_unique<WsClient>(server_address_);
    setup_callbacks();
}

void WebSocketClient::setup_callbacks() {
    if (!client_) return;

    client_->on_open = [this](shared_ptr<WsClient::Connection> connection) {
        {
            lock_guard<mutex> lock(connection_mutex_);
            connection_ = connection;
            connected_ = true;
            is_reconnecting_ = false;
        }
        connection_cv_.notify_all();

        // 重置重连状态
        reset_reconnect_state();

        // 刚连上，刷新存活时间，避免看门狗立即误判
        mark_alive();

        // 通知连接成功
        if (connection_callback_) {
            connection_callback_(true);
        }

        cout << "webSocket已连接。" << endl;
    };

    client_->on_message = [this](shared_ptr<WsClient::Connection> connection, shared_ptr<WsClient::Message> message) {
        // 收到任何业务消息也视为对端存活
        mark_alive();
        string message_str = message->string();
        if (message_callback_) {
            message_callback_(message_str);
        }
    };

    // 收到 pong：对端存活的明确证据
    client_->on_pong = [this](shared_ptr<WsClient::Connection> /*connection*/) {
        SPDLOG_DEBUG("收到pong");
        mark_alive();
    };

    // 收到对端 ping 时库会自动回 pong，这里也刷新存活时间
    client_->on_ping = [this](shared_ptr<WsClient::Connection> /*connection*/) {
        mark_alive();
    };

    client_->on_close = [this](shared_ptr<WsClient::Connection> /*connection*/, int status, const string & /*reason*/) {
        bool was_connected = false;
        {
            lock_guard<mutex> lock(connection_mutex_);
            was_connected = connected_;
            connected_ = false;
            connection_.reset();
        }

        cout << "WebSocket关闭，状态码: " << status << endl;

        // 通知连接断开
        if (connection_callback_ && was_connected) {
            connection_callback_(false);
        }

        // 如果不是主动停止，则尝试重连
        if (!should_stop_ && reconnect_config_.enable_auto_reconnect && !is_reconnecting_) {
            schedule_reconnect();
        }
    };

    client_->on_error = [this](shared_ptr<WsClient::Connection> /*connection*/, const SimpleWeb::error_code &ec) {
        bool was_connected = false;
        {
            lock_guard<mutex> lock(connection_mutex_);
            was_connected = connected_;
            connected_ = false;
            connection_.reset();
        }

        cout << "WebSocket错误: " << ec << ", 错误信息: " << ec.message() << endl;

        // 通知连接断开
        if (connection_callback_ && was_connected) {
            connection_callback_(false);
        }

        // 如果不是主动停止，则尝试重连
        if (!should_stop_ && reconnect_config_.enable_auto_reconnect && !is_reconnecting_) {
            schedule_reconnect();
        }
    };
}

void WebSocketClient::start_client() {
    std::cout << "正在连接到webSocket服务器: " << server_address_ << "..." << std::endl;
    should_stop_ = false;
    internal_start();

    // 启动心跳看门狗
    if (heartbeat_config_.enable_heartbeat) {
        start_heartbeat();
    }
}

void WebSocketClient::internal_start() {
    if (!client_) {
        create_client();
    }

    // 在新线程中启动客户端
    std::thread([this]() {
        try {
            client_->start();
        } catch (const exception& e) {
            cout << "WebSocket启动失败: " << e.what() << endl;

            // 启动失败也尝试重连
            if (!should_stop_ && reconnect_config_.enable_auto_reconnect && !is_reconnecting_) {
                schedule_reconnect();
            }
        }
    }).detach();
}

void WebSocketClient::stop_client() {
    SPDLOG_INFO("关闭连接");
    should_stop_ = true;

    // 先停掉心跳看门狗，避免它在关闭过程中触发重连
    stop_heartbeat();

    // 关闭连接
    {
        lock_guard<mutex> lock(connection_mutex_);
        if (connection_) {
            connection_->send_close(1000);  // 正常关闭
        }
    }

    // 停止客户端
    if (client_) {
        client_->stop();
    }

    // 唤醒可能在 send_message 中等待的线程
    connection_cv_.notify_all();

    // 等待重连线程结束
    if (reconnect_thread_.joinable()) {
        reconnect_thread_.join();
    }
}

void WebSocketClient::schedule_reconnect() {
    // 检查是否已经在重连
    bool expected = false;
    if (!is_reconnecting_.compare_exchange_strong(expected, true)) {
        return;  // 已经在重连中
    }

    // 检查是否超过最大重连次数
    if (reconnect_config_.max_reconnect_attempts > 0 &&
        reconnect_attempts_ >= reconnect_config_.max_reconnect_attempts) {
        cout << "已达到最大重连次数: " << reconnect_config_.max_reconnect_attempts << endl;
        is_reconnecting_.store(false);
        return;
    }

    // 如果之前的重连线程还在运行，等待它结束
    if (reconnect_thread_.joinable()) reconnect_thread_.join();

    reconnect_thread_ = thread([this]() {
        // 增加重连次数
        int attempt = ++reconnect_attempts_;
        int delay = current_delay_ms_.load();

        cout << "将在 " << delay << " 毫秒后进行第 " << attempt << " 次重连..." << endl;
        this_thread::sleep_for(chrono::milliseconds(delay));
        if (should_stop_) {
            is_reconnecting_.store(false);
            return;
        }

        cout << "正在尝试重连..." << endl;

        // 更新延迟时间（指数退避）
        int next_delay = static_cast<int>(delay * reconnect_config_.backoff_multiplier);
        current_delay_ms_ = min(next_delay, reconnect_config_.max_delay_ms);

        // 停止旧的客户端
        if (client_) {
            client_->stop();
            // 等待一小段时间确保清理完成
            this_thread::sleep_for(chrono::milliseconds(100));
        }

        // 重新创建客户端并启动
        create_client();
        internal_start();

        is_reconnecting_.store(false);
    });
}

void WebSocketClient::reset_reconnect_state() {
    reconnect_attempts_ = 0;
    current_delay_ms_ = reconnect_config_.initial_delay_ms;
}

void WebSocketClient::reconnect() {
    // 先断开现有连接
    {
        lock_guard<mutex> lock(connection_mutex_);
        if (connection_) {
            connection_->send_close(1000);
        }
        connected_ = false;
        connection_.reset();
    }
    // 停止客户端
    if (client_) {
        client_->stop();
    }
    // 触发重连
    schedule_reconnect();
    SPDLOG_INFO("重连");
}

bool WebSocketClient::send_message(const std::string& message) {
    unique_lock<mutex> lock(connection_mutex_);
    // 如果未连接，等待一小段时间看是否能连接
    if (!connected_) {
        // 等待连接建立，最多等待5秒（should_stop_ 时立即返回）
        if (!connection_cv_.wait_for(lock, chrono::seconds(5), [this] {
                return connected_.load() || should_stop_.load();
            })) {
            cout << "WebSocket连接超时！" << endl;
            return false;
        }
        if (should_stop_) return false;
    }
    if (connected_ && connection_) {
        try {
            auto send_stream = make_shared<WsClient::SendStream>();
            *send_stream << message;
            connection_->send(send_stream, [this](const SimpleWeb::error_code& ec){
                if (ec) SPDLOG_WARN("WS实际发送失败: {}", ec.message());
            });
            return true;
        } catch (const exception& e) {
            cout << "发送错误: " << e.what() << endl;
            return false;
        }
    }
    return false;
}

void WebSocketClient::set_message_callback(std::function<void(const std::string&)> callback) {
    message_callback_ = callback;
}

void WebSocketClient::set_connection_callback(std::function<void(bool)> callback) {
    connection_callback_ = callback;
}

bool WebSocketClient::is_connected() const {
    lock_guard<mutex> lock(connection_mutex_);
    return connected_;
}

void WebSocketClient::set_reconnect_config(const ReconnectConfig& config) {
    reconnect_config_ = config;
    current_delay_ms_ = config.initial_delay_ms;
}

void WebSocketClient::set_heartbeat_config(const HeartbeatConfig& config) {
    heartbeat_config_ = config;
}

// ===================== 心跳看门狗 =====================

void WebSocketClient::mark_alive() {
    last_alive_ms_.store(now_ms());
}

void WebSocketClient::start_heartbeat() {
    bool expected = false;
    if (!heartbeat_running_.compare_exchange_strong(expected, true)) {
        return;  // 已在运行
    }
    mark_alive();
    heartbeat_thread_ = std::thread(&WebSocketClient::heartbeat_loop, this);
}

void WebSocketClient::stop_heartbeat() {
    heartbeat_running_.store(false);
    heartbeat_cv_.notify_all();
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

void WebSocketClient::heartbeat_loop() {
    while (heartbeat_running_ && !should_stop_) {
        // 等待一个 ping 间隔，期间可被 stop 唤醒
        {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            heartbeat_cv_.wait_for(lock,
                std::chrono::milliseconds(heartbeat_config_.ping_interval_ms),
                [this] { return !heartbeat_running_.load() || should_stop_.load(); });
        }
        if (!heartbeat_running_ || should_stop_) break;

        // 未连接 或 正在重连：跳过本轮，避免误判和重连风暴
        if (!connected_ || is_reconnecting_) {
            continue;
        }

        // 发送协议层 ping：opcode 9(ping) | 0x80(fin) = 137
        {
            lock_guard<mutex> lock(connection_mutex_);
            if (connection_) {
                try {
                    auto ping = make_shared<WsClient::SendStream>();
                    connection_->send(ping, nullptr, 137);
                } catch (const exception& e) {
                    cout << "ping发送异常: " << e.what() << endl;
                }
            }
        }

        // 检查存活超时：超过 pong_timeout 未收到任何 pong/消息 -> 判定半开连接
        long long elapsed = now_ms() - last_alive_ms_.load();
        if (elapsed > heartbeat_config_.pong_timeout_ms) {
            // 再次确认仍处于"已连接但实际半开"状态，且不在重连中
            if (connected_ && !is_reconnecting_ && !should_stop_) {
                SPDLOG_WARN("pong超时({}ms未收到对端响应)，判定半开连接，主动重连", elapsed);
                cout << "pong超时，判定半开连接，主动重连" << endl;
                // 刷新一次，避免 reconnect 期间重复触发
                mark_alive();
                reconnect();
            }
        }
    }
}