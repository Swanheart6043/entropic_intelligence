#pragma once
#include <atomic>
#include <chrono>
#include <thread>
#include <string_view>
#include <zmq.hpp>
#include "cloud_gateway_client.hpp"
#include "hikvision_isapi_client.hpp"

class TimeSynchronizer {
public:
    TimeSynchronizer();
    ~TimeSynchronizer();
    void start();
    void stop();

private:
    static constexpr std::chrono::seconds kChargeSyncInterval{3600}; // 充电期间校时间隔
    static constexpr std::chrono::seconds kBootRetryInterval{30}; // 开机校时失败重试间隔
    static constexpr std::chrono::milliseconds kSyncThreshold{1000}; // 偏差阈值,小于此值不设时钟

    static constexpr int kZmqTimeoutMs = 3000;  // 感知中心 REQ 收发超时
    static constexpr int kLingerMs = 1000;
    static constexpr int kSndHwm = 100;
    static constexpr const char* kPerceiverEndpoint = "tcp://10.10.10.30:2430";

    std::atomic<bool> running{false};
    std::thread worker_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_from_control{ctx, ZMQ_SUB};
    zmq::socket_t socket_to_perceiver{ctx, zmq::socket_type::req};
    cloud_gateway::CloudGatewayClient cloud_gateway_;

    void time_synchronous_loop();
    bool do_sync();
    void rebuild_perceiver_socket();
    void sync_oneself(int64_t date_time);
    void sync_perceiver(int64_t date_time);
    void sync_high_definition_camera(int64_t date_time);
    void sync_infrared_camera(int64_t date_time);
    void sync_front_camera(int64_t date_time);
    void sync_back_camera(int64_t date_time);
    void sync_left_camera(int64_t date_time);
    void sync_right_camera(int64_t date_time);
    void sync_merlin_camera(std::string_view camera_name, std::string_view host, int64_t date_time);
};
