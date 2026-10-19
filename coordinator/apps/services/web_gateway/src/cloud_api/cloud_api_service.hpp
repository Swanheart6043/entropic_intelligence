#pragma once

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>
#include <zmq.hpp>

#include "cloud_platform_client.hpp"
#include "cloud_outbox.hpp"
#include "config_parser.hpp"

class CloudApiService {
public:
    explicit CloudApiService(WebSocketClient& websocket, std::size_t rpc_worker_count = 3);
    ~CloudApiService();

    void start();
    void stop();

private:
    static constexpr const char* kBackendEndpoint = "inproc://cloud_api_rpc_workers";

    Config* config_{Config::getInstance()};
    CloudPlatformClient platform_client_;
    CloudOutbox outbox_;
    const std::size_t rpc_worker_count_;

    std::atomic<bool> running_{false};
    zmq::context_t context_{1};
    std::thread broker_thread_;
    std::vector<std::thread> rpc_workers_;
    std::thread outbox_thread_;

    std::mutex broker_ready_mutex_;
    std::condition_variable broker_ready_cv_;
    bool broker_ready_{false};
    std::string broker_error_;

    std::mutex outbox_mutex_;
    std::condition_variable outbox_cv_;

    void broker_loop();
    void rpc_worker_loop(std::size_t worker_index);
    void outbox_loop();

    nlohmann::json handle_rpc(
        const nlohmann::json& request,
        const std::string& binary_payload
    );
    void process_outbox_job(CloudOutbox::Job& job);
    void schedule_retry(
        CloudOutbox::Job& job,
        const CloudPlatformResult& failure
    );
};
