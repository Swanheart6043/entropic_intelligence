#include "common/wss_connector.hpp"
#include <future>
#include <iostream>
#include <stdexcept>

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<typename Predicate>
bool wait_until(Predicate predicate) {
    for (int i = 0; i < 500; ++i) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: websocket_send_test port mode");
        const std::string mode = argv[2];
        if (mode == "normal") {
            WebSocketClient lifecycle("ws://127.0.0.1:1/test");
            WebSocketClient::HeartbeatConfig disabled;
            disabled.enable_heartbeat = false;
            lifecycle.set_heartbeat_config(disabled);
            for (int i = 0; i < 20; ++i) {
                lifecycle.start_client();
                lifecycle.stop_client();
            }
        }
        WebSocketClient::ReconnectConfig config;
        config.initial_delay_ms = 30;
        config.max_delay_ms = 100;
        WebSocketClient client("ws://127.0.0.1:" + std::string(argv[1]) + "/test", config);
        WebSocketClient::HeartbeatConfig heartbeat;
        heartbeat.enable_heartbeat = false;
        client.set_heartbeat_config(heartbeat);
        require(!client.send_message("offline"), "disconnected async send must fail");
        require(!client.send_message_and_wait("offline"), "disconnected outbox send must fail");
        std::atomic<int> connections{0};
        std::atomic<bool> callback_sent{false};
        client.set_connection_callback([&](bool connected) { if (connected) ++connections; });
        client.set_message_callback([&](const std::string&) {
            const bool guard = !client.send_message_and_wait("must-not-block-io");
            callback_sent = guard && client.send_message("callback-reply");
        });
        client.start_client();
        require(wait_until([&] { return client.is_connected(); }), "initial connection did not recover");
        if (mode == "blackhole" || mode == "reset") {
            const bool sent = client.send_message_and_wait(std::string(8 * 1024 * 1024, 'x'), 200);
            require(!sent, "stalled or reset socket must not be reported successful");
            require(wait_until([&] { return connections >= 2 && client.is_connected(); }), "reconnect after failed send");
        }
        require(client.send_message_and_wait("event-without-cloud-ack", 1000), "local write without cloud ACK");
        if (mode == "normal" || mode == "retry") {
            require(wait_until([&] { return callback_sent.load(); }), "downlink reply must not deadlock I/O");
        }
        client.stop_client();
        require(!client.send_message("stopped"), "stopped client must fail sends");
        std::cout << "PASS: websocket " << mode << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
