#pragma once
#include <atomic>
#include <string>
#include "../action_executor.hpp"

class TargetDetector {
public:
    static TargetDetector& instance();
    TargetDetector(const TargetDetector&) = delete;
    TargetDetector& operator=(const TargetDetector&) = delete;
    TargetDetector(TargetDetector&&) = delete;
    TargetDetector& operator=(TargetDetector&&) = delete;
    
    void start(const ActionExecution& action, const std::string& point_name, const std::string& report_uuid);
    void stop();
    bool is_running() const;

private:
    static constexpr int kZmqTimeoutMs = 3000;
    static constexpr int kLingerMs = 1000;
    static constexpr int kSndHwm = 100;
    static constexpr const char* kPerceiverEndpoint = "tcp://10.10.10.30:2430";

    std::atomic<bool> running{false};
    std::mutex wake_mutex;
    std::condition_variable wake;

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_perceiver{ctx, zmq::socket_type::req};
    
    TargetDetector();
    ~TargetDetector();
    
    void breaking_into_penalty_area(std::string direction, std::string control);
    void door_opened_abnormally(std::string direction, std::string control);
    void sleeping(std::chrono::milliseconds duration);
    void rebuild_perceiver_socket();
};