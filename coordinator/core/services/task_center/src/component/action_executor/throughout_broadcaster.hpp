#pragma once
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

class ThroughoutBroadcaster {
public:
    static ThroughoutBroadcaster& getInstance() {
        static ThroughoutBroadcaster instance;
        return instance;
    }
    ThroughoutBroadcaster(const ThroughoutBroadcaster&) = delete;
    ThroughoutBroadcaster& operator=(const ThroughoutBroadcaster&) = delete;
    
    void start(const std::string& text, int interval_ms);
    void stop();

    std::string get_broadcast_text();
    int get_broadcast_gap();

    bool is_running() const;

private:
    std::atomic<bool> running{false};

    std::thread worker;
    std::mutex mutex;
    std::mutex cv_m;
    std::condition_variable cv;

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::push};

    std::string text;
    int gap_ms{1000};

    ThroughoutBroadcaster();
    ~ThroughoutBroadcaster() noexcept;
    
    void worker_loop();
    void sleeping(std::chrono::milliseconds duration);
};