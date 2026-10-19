#pragma once
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <optional>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "cloud_gateway_client.hpp"

struct AlarmAction {
    std::string alarm_type;
    std::string action_type;
    std::string value1;
    std::string value2;
    int wait_ms;
};

inline void to_json(nlohmann::json& j, const AlarmAction& a) {
    j = nlohmann::json{
        {"alarm_type", a.alarm_type},
        {"action_type", a.action_type},
        {"value1", a.value1},
        {"value2", a.value2},
        {"wait_ms", a.wait_ms}
    };
}

inline void from_json(const nlohmann::json& j, AlarmAction& a) {
    a.alarm_type = j.value("alarm_type", "");
    a.action_type = j.value("action_type", "");
    a.value1 = j.value("value1", "");
    a.value2 = j.value("value2", "");
    a.wait_ms = j.value("wait_ms", 0);
}

struct Detection {
    int class_id;
    std::string class_name;
    float confidence;
    int x1, y1, x2, y2;
    int width, height;
};

struct DetectionResult {
    std::string type;
    std::string detection_type;
    int frame_id;
    double timestamp;
    int image_width;
    int image_height;
    std::vector<Detection> detections;
    float avg_confidence;
    float max_confidence;
    std::string all_confidences;
    std::vector<uint8_t> image_data;
};

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t max_size) : max_size_(max_size) {}

    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() >= max_size_) {
                queue_.pop_front(); // 丢最旧的，保留最新检测
                ++dropped_;
            }
            queue_.push_back(std::move(value));
        }
        cv_.notify_one();
    }

    // 返回 nullopt 表示被唤醒退出
    std::optional<T> pop(const std::atomic<bool>& running) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return !queue_.empty() || !running; });
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    void notify_all() { 
        cv_.notify_all(); 
    }

    size_t dropped() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    size_t max_size_;
    size_t dropped_ = 0;
};

class PerceiverCenterServer {
public:
    PerceiverCenterServer();
    ~PerceiverCenterServer();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    BoundedQueue<std::string> message_queue{32};
    std::thread receiver_thread;
    std::thread worker_thread;
    std::thread sender_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_from_perceiver{ctx, zmq::socket_type::pull};
    zmq::socket_t socket_req_task_center{ctx, zmq::socket_type::dealer};
    zmq::socket_t socket_push_task_center{ctx, zmq::socket_type::push};

    std::shared_ptr<spdlog::logger> logger;
    cloud_gateway::CloudGatewayClient cloud_gateway_;

    void receiver_loop();
    void worker_loop();
    void sender_loop();

    void handle_message(const std::string& message);
    DetectionResult parse_detection(const std::string& message);
    void send_alarm_actions(const std::string& type, int code);
    std::vector<AlarmAction> query_alarm_actions(int code);

    std::string request_task_uuid();

    std::vector<uint8_t> decode_base64(const std::string& encoded);
    std::string format_now_date_to_string();

    void clear_old_logs(const std::string& dir, int max_day);
    void create_log(const std::string& path);
};
