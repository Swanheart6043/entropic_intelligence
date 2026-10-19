#pragma once
#include <zmq.hpp>
#include "websocket_client.hpp"
#include "config_parser.hpp"

class LiDarMonitor {
public:
    explicit LiDarMonitor();
    ~LiDarMonitor();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();
    std::atomic<bool> running{false};

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_audio{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_web{ctx, zmq::socket_type::push};
    std::unique_ptr<WebSocketClient> websocket;

    void listening(const std::string& message);
    void handle_someone_on_the_lid();
    void handle_laser_anomalies();
    void handle_gyroscope_malfunctions();
    void handle_ultrasound_anomalies();
};