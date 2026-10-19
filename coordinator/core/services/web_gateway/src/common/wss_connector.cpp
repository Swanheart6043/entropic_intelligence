#include "wss_connector.hpp"

#include <algorithm>
#include <future>
#include <vector>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

namespace {

thread_local const WebSocketClient* current_io_client = nullptr;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.compare(0, prefix.size(), prefix) == 0;
}

} // namespace

class WebSocketClient::Transport {
public:
    using OpenHandler = std::function<void()>;
    using MessageHandler = std::function<void(const std::string&)>;
    using CloseHandler = std::function<void(int, const std::string&)>;
    using ErrorHandler = std::function<void(const std::string&)>;
    using AliveHandler = std::function<void()>;

    virtual ~Transport() = default;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool send(const std::string& message, std::function<void(bool)> completion) = 0;
    virtual bool ping() = 0;
    virtual void close() = 0;
};

namespace {

template <typename SocketType>
class SimpleWebTransport final : public WebSocketClient::Transport {
public:
    using Client = SimpleWeb::SocketClient<SocketType>;
    using Connection = typename Client::Connection;
    using OpenHandler = WebSocketClient::Transport::OpenHandler;
    using MessageHandler = WebSocketClient::Transport::MessageHandler;
    using CloseHandler = WebSocketClient::Transport::CloseHandler;
    using ErrorHandler = WebSocketClient::Transport::ErrorHandler;
    using AliveHandler = WebSocketClient::Transport::AliveHandler;

    SimpleWebTransport(
        std::unique_ptr<Client> client,
        OpenHandler on_open,
        MessageHandler on_message,
        CloseHandler on_close,
        ErrorHandler on_error,
        AliveHandler on_alive
    ) : client_(std::move(client)),
        on_open_(std::move(on_open)),
        on_message_(std::move(on_message)),
        on_close_(std::move(on_close)),
        on_error_(std::move(on_error)),
        on_alive_(std::move(on_alive)) {
        // Own the event loop explicitly so stop() cannot race start() resetting
        // a previously stopped internal io_service during a fast reconnect.
        client_->io_service = std::make_shared<typename decltype(client_->io_service)::element_type>();
        client_->on_open = [this](std::shared_ptr<Connection> connection) {
            {
                std::lock_guard<std::mutex> lock(connection_mutex_);
                if (stopped_) return;
                connection_ = std::move(connection);
            }
            on_open_();
        };
        client_->on_message = [this](
            std::shared_ptr<Connection>,
            std::shared_ptr<typename Client::Message> message
        ) {
            on_alive_();
            on_message_(message->string());
        };
        client_->on_pong = [this](std::shared_ptr<Connection>) { on_alive_(); };
        client_->on_ping = [this](std::shared_ptr<Connection>) { on_alive_(); };
        client_->on_close = [this](
            std::shared_ptr<Connection>,
            int status,
            const std::string& reason
        ) {
            {
                std::lock_guard<std::mutex> lock(connection_mutex_);
                connection_.reset();
            }
            on_close_(status, reason);
        };
        client_->on_error = [this](
            std::shared_ptr<Connection>,
            const SimpleWeb::error_code& error
        ) {
            {
                std::lock_guard<std::mutex> lock(connection_mutex_);
                connection_.reset();
            }
            on_error_(error.message());
        };
    }

    void start() override {
        {
            std::lock_guard<std::mutex> lock(lifecycle_mutex_);
            if (stopped_) return;
            client_->start(); // External io_service: sets up asynchronous connect only.
        }
        client_->io_service->run();
    }
    void stop() override {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        stopped_ = true;
        std::vector<std::shared_ptr<PendingWrite>> pending;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            connection_.reset();
            pending.swap(pending_writes_);
        }
        for (const auto& write : pending) finish_write(write, false, "连接停止或重建");
        client_->stop();
        client_->io_service->stop();
    }

    bool send(const std::string& message, std::function<void(bool)> completion) override {
        std::shared_ptr<Connection> connection;
        auto write = std::make_shared<PendingWrite>();
        write->completion = std::move(completion);
        write->bytes = message.size();
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            connection = connection_;
            pending_writes_.erase(std::remove_if(pending_writes_.begin(), pending_writes_.end(),
                [](const auto& pending) { return pending->completed.load(); }), pending_writes_.end());
            if (connection) pending_writes_.push_back(write);
        }
        if (!connection) {
            SPDLOG_INFO("WebSocket发送云端失败: 连接不可用, bytes={}", message.size());
            return false;
        }
        try {
            auto stream = std::make_shared<typename Client::SendStream>();
            *stream << message;
            connection->send(stream, [write, on_error = on_error_](const SimpleWeb::error_code& error) {
                if (finish_write(write, !error, error.message()) && error) on_error(error.message());
            });
            return true;
        } catch (const std::exception& e) {
            finish_write(write, false, e.what());
            return false;
        }
    }

    bool ping() override {
        std::shared_ptr<Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            connection = connection_;
        }
        if (!connection) return false;
        try {
            auto stream = std::make_shared<typename Client::SendStream>();
            connection->send(stream, [on_error = on_error_](const SimpleWeb::error_code& error) {
                if (error) {
                    SPDLOG_INFO("WebSocket发送云端失败: ping写入错误={}", error.message());
                    on_error(error.message());
                }
            }, 137);
            return true;
        } catch (const std::exception& e) {
            SPDLOG_INFO("WebSocket发送云端失败: ping异常={}", e.what());
            return false;
        }
    }

    void close() override {
        std::shared_ptr<Connection> connection;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            connection = connection_;
        }
        if (connection) connection->send_close(1000);
    }

private:
    struct PendingWrite {
        std::atomic<bool> completed{false};
        std::size_t bytes{0};
        std::function<void(bool)> completion;
    };
    static bool finish_write(const std::shared_ptr<PendingWrite>& write, bool ok, const std::string& reason) {
        if (write->completed.exchange(true)) return false;
        if (!ok) SPDLOG_INFO("WebSocket发送云端失败: reason={}, bytes={}", reason, write->bytes);
        if (write->completion) write->completion(ok);
        return true;
    }
    std::vector<std::shared_ptr<PendingWrite>> pending_writes_;
    std::mutex lifecycle_mutex_;
    std::atomic<bool> stopped_{false};
    std::unique_ptr<Client> client_;
    std::mutex connection_mutex_;
    std::shared_ptr<Connection> connection_;
    OpenHandler on_open_;
    MessageHandler on_message_;
    CloseHandler on_close_;
    ErrorHandler on_error_;
    AliveHandler on_alive_;
};

} // namespace

WebSocketClient::WebSocketClient(const std::string& server_address)
    : WebSocketClient(server_address, ReconnectConfig{}, TlsConfig{}) {}

WebSocketClient::WebSocketClient(
    const std::string& server_address,
    const ReconnectConfig& config
) : WebSocketClient(server_address, config, TlsConfig{}) {}

WebSocketClient::WebSocketClient(
    const std::string& server_address,
    const TlsConfig& tls_config
) : WebSocketClient(server_address, ReconnectConfig{}, tls_config) {}

WebSocketClient::WebSocketClient(
    const std::string& server_address,
    const ReconnectConfig& config,
    const TlsConfig& tls_config
) : server_address_(server_address), reconnect_config_(config), tls_config_(tls_config) {
    current_delay_ms_ = reconnect_config_.initial_delay_ms;
    create_client();
}

WebSocketClient::~WebSocketClient() {
    stop_client();
}

void WebSocketClient::create_client() {
    std::string address = server_address_;
    const bool secure = starts_with(address, "wss://");
    if (secure) {
        address.erase(0, 6);
    } else if (starts_with(address, "ws://")) {
        address.erase(0, 5);
    } else if (address.find("://") != std::string::npos) {
        throw std::invalid_argument("websocket URL scheme must be ws:// or wss://");
    }

    const auto on_open = [this] { transport_opened(); };
    const auto on_message = [this](const std::string& message) { transport_message(message); };
    const auto on_close = [this](int status, const std::string& reason) {
        transport_closed(status, reason);
    };
    const auto on_error = [this](const std::string& message) { transport_error(message); };
    const auto on_alive = [this] { mark_alive(); };

    std::lock_guard<std::mutex> lock(connection_mutex_);
    if (secure) {
        auto client = std::make_unique<SimpleWeb::SocketClient<SimpleWeb::WSS>>(
            address,
            tls_config_.verify_certificate,
            tls_config_.client_cert_file,
            tls_config_.client_key_file,
            tls_config_.ca_cert_file
        );
        transport_ = std::make_shared<SimpleWebTransport<SimpleWeb::WSS>>(
            std::move(client), on_open, on_message, on_close, on_error, on_alive
        );
    } else {
        auto client = std::make_unique<SimpleWeb::SocketClient<SimpleWeb::WS>>(address);
        transport_ = std::make_shared<SimpleWebTransport<SimpleWeb::WS>>(
            std::move(client), on_open, on_message, on_close, on_error, on_alive
        );
    }
}

void WebSocketClient::transport_opened() {
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        connected_ = true;
        is_reconnecting_ = false;
    }
    connection_cv_.notify_all();
    reset_reconnect_state();
    mark_alive();
    if (connection_callback_) connection_callback_(true);
    SPDLOG_INFO("WebSocket已连接: {}", server_address_);
}

void WebSocketClient::transport_message(const std::string& message) {
    mark_alive();
    if (message_callback_) message_callback_(message);
}

void WebSocketClient::transport_closed(int status, const std::string& reason) {
    bool was_connected = false;
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        was_connected = connected_.exchange(false);
    }
    connection_cv_.notify_all();
    SPDLOG_WARN("WebSocket关闭: status={}, reason={}", status, reason);
    if (connection_callback_ && was_connected) connection_callback_(false);
    if (!should_stop_) request_reconnect();
}

void WebSocketClient::transport_error(const std::string& message) {
    bool was_connected = false;
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        was_connected = connected_.exchange(false);
    }
    connection_cv_.notify_all();
    SPDLOG_ERROR("WebSocket错误: {}", message);
    if (connection_callback_ && was_connected) connection_callback_(false);
    if (!should_stop_) request_reconnect();
}

void WebSocketClient::start_client() {
    if (reconnect_thread_.joinable()) return;
    should_stop_ = false;
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        reconnect_requested_ = false;
        manual_reconnect_ = false;
    }
    reconnect_thread_ = std::thread(&WebSocketClient::connection_loop, this);
    if (heartbeat_config_.enable_heartbeat) start_heartbeat();
}

void WebSocketClient::stop_client() {
    should_stop_ = true;
    reconnect_cv_.notify_all();
    connected_ = false;
    connection_cv_.notify_all();
    stop_heartbeat();
    if (reconnect_thread_.joinable()) reconnect_thread_.join();
}

// One supervisor owns client_thread_ and transport replacement. I/O callbacks
// only signal it; they never join a thread that may itself be waiting for them.
void WebSocketClient::connection_loop() {
    while (!should_stop_) {
        std::shared_ptr<Transport> transport;
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            transport = transport_;
        }
        if (!transport) {
            create_client();
            std::lock_guard<std::mutex> lock(connection_mutex_);
            transport = transport_;
        }
        SPDLOG_INFO("正在连接WebSocket: {}", server_address_);
        is_reconnecting_ = true;
        client_thread_ = std::thread([this, transport] {
            current_io_client = this;
            try {
                transport->start();
            } catch (const std::exception& e) {
                transport_error(e.what());
            }
            current_io_client = nullptr;
            if (!should_stop_) request_reconnect();
        });
        std::unique_lock<std::mutex> lock(reconnect_mutex_);
        reconnect_cv_.wait(lock, [this] { return should_stop_ || reconnect_requested_; });
        reconnect_requested_ = false;
        lock.unlock();
        transport->stop();
        if (client_thread_.joinable()) client_thread_.join();
        {
            std::lock_guard<std::mutex> connection_lock(connection_mutex_);
            connected_ = false;
            transport_.reset();
        }
        if (should_stop_) break;
        lock.lock();
        // Ignore close/error notifications caused by stopping the old transport.
        reconnect_requested_ = false;
        if (!manual_reconnect_ && (!reconnect_config_.enable_auto_reconnect ||
            (reconnect_config_.max_reconnect_attempts > 0 &&
             reconnect_attempts_ >= reconnect_config_.max_reconnect_attempts))) {
            SPDLOG_INFO("WebSocket自动重连已暂停，等待手动重连");
            reconnect_cv_.wait(lock, [this] { return should_stop_ || manual_reconnect_; });
        }
        if (should_stop_) break;
        const int delay = manual_reconnect_ ? 0 : current_delay_ms_.load();
        manual_reconnect_ = false;
        const int attempt = ++reconnect_attempts_;
        SPDLOG_INFO("WebSocket将在{}ms后进行第{}次重连", delay, attempt);
        reconnect_cv_.wait_for(lock, std::chrono::milliseconds(delay),
                               [this] { return should_stop_ || manual_reconnect_; });
        manual_reconnect_ = false;
        reconnect_requested_ = false;
        current_delay_ms_ = std::min(
            static_cast<int>(std::max(delay, reconnect_config_.initial_delay_ms) *
                             reconnect_config_.backoff_multiplier), reconnect_config_.max_delay_ms);
    }
    is_reconnecting_ = false;
}

void WebSocketClient::request_reconnect(bool manual) {
    {
        std::lock_guard<std::mutex> lock(reconnect_mutex_);
        reconnect_requested_ = true;
        manual_reconnect_ = manual_reconnect_ || manual;
    }
    reconnect_cv_.notify_all();
}

void WebSocketClient::reset_reconnect_state() {
    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    reconnect_attempts_ = 0;
    current_delay_ms_ = reconnect_config_.initial_delay_ms;
}

void WebSocketClient::reconnect() {
    connected_ = false;
    connection_cv_.notify_all();
    request_reconnect(true);
}

bool WebSocketClient::send_async(const std::string& message, std::function<void(bool)> completion,
                                 std::shared_ptr<Transport>* used_transport) {
    std::shared_ptr<Transport> transport;
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        if (connected_ && !should_stop_) transport = transport_;
    }
    if (!transport) {
        SPDLOG_INFO("WebSocket发送云端失败: 未连接或连接已断开, bytes={}", message.size());
        return false;
    }
    if (used_transport) *used_transport = transport;
    return transport->send(message, std::move(completion));
}

bool WebSocketClient::send_message(const std::string& message) {
    return send_async(message, nullptr);
}

bool WebSocketClient::send_message_and_wait(const std::string& message, int timeout_ms) {
    // The downlink callback runs on this thread: waiting there prevents the
    // socket write callback from running and would manufacture a timeout.
    // A thread-local flag avoids reading a std::thread while the supervisor joins it.
    if (current_io_client == this || timeout_ms <= 0) {
        SPDLOG_INFO("WebSocket发送云端失败: 本地写入等待必须在工作线程执行且超时必须为正");
        return false;
    }
    auto result = std::make_shared<std::promise<bool>>();
    auto future = result->get_future();
    std::shared_ptr<Transport> used_transport;
    if (!send_async(message, [result](bool ok) { result->set_value(ok); }, &used_transport)) return false;
    if (future.wait_for(std::chrono::milliseconds(timeout_ms)) != std::future_status::ready) {
        SPDLOG_INFO("WebSocket发送云端失败: 本地写入超时, timeout_ms={}, bytes={}", timeout_ms, message.size());
        // Do not reset a new connection because an old write timed out.
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            if (transport_ == used_transport) {
                connected_ = false;
                request_reconnect();
            }
        }
        return false;
    }
    return future.get();
}

void WebSocketClient::set_message_callback(
    std::function<void(const std::string&)> callback
) {
    message_callback_ = std::move(callback);
}

void WebSocketClient::set_connection_callback(std::function<void(bool)> callback) {
    connection_callback_ = std::move(callback);
}

bool WebSocketClient::is_connected() const {
    std::lock_guard<std::mutex> lock(connection_mutex_);
    return connected_;
}

void WebSocketClient::set_reconnect_config(const ReconnectConfig& config) {
    std::lock_guard<std::mutex> lock(reconnect_mutex_);
    reconnect_config_ = config;
    current_delay_ms_ = config.initial_delay_ms;
}

void WebSocketClient::set_heartbeat_config(const HeartbeatConfig& config) {
    heartbeat_config_ = config;
}

void WebSocketClient::mark_alive() {
    last_alive_ms_ = now_ms();
}

void WebSocketClient::start_heartbeat() {
    bool expected = false;
    if (!heartbeat_running_.compare_exchange_strong(expected, true)) return;
    mark_alive();
    heartbeat_thread_ = std::thread(&WebSocketClient::heartbeat_loop, this);
}

void WebSocketClient::stop_heartbeat() {
    heartbeat_running_ = false;
    heartbeat_cv_.notify_all();
    if (heartbeat_thread_.joinable() && heartbeat_thread_.get_id() != std::this_thread::get_id()) {
        heartbeat_thread_.join();
    }
}

void WebSocketClient::heartbeat_loop() {
    while (heartbeat_running_ && !should_stop_) {
        {
            std::unique_lock<std::mutex> lock(heartbeat_mutex_);
            heartbeat_cv_.wait_for(
                lock,
                std::chrono::milliseconds(heartbeat_config_.ping_interval_ms),
                [this] { return !heartbeat_running_.load() || should_stop_.load(); }
            );
        }
        if (!heartbeat_running_ || should_stop_) break;
        if (!connected_ || is_reconnecting_) continue;

        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            if (transport_) transport_->ping();
        }
        const long long elapsed = now_ms() - last_alive_ms_.load();
        if (elapsed > heartbeat_config_.pong_timeout_ms &&
            connected_ && !is_reconnecting_ && !should_stop_) {
            SPDLOG_INFO("WebSocket连接失效: pong超时({}ms)，主动重连", elapsed);
            mark_alive();
            reconnect();
        }
    }
}
