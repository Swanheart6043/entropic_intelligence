#include "throughout_broadcaster.hpp"

ThroughoutBroadcaster::ThroughoutBroadcaster() {
    socket.connect("ipc://@edge_gateway_push_pull");
}

ThroughoutBroadcaster::~ThroughoutBroadcaster() {
    try {
        stop();
    } catch (std::exception &e) {
        SPDLOG_ERROR("全程语音播报器析构异常: {}", e.what());
    }
}

void ThroughoutBroadcaster::start(const std::string& content, int interval_ms) {
    std::lock_guard<std::mutex> lock(mutex);
    if (running.load()) {
        SPDLOG_WARN("全程播报已在运行，不做任何处理");
        return;
    }
    text = content;
    gap_ms = interval_ms;
    running.store(true);
    worker = std::thread(&ThroughoutBroadcaster::worker_loop, this);
}

void ThroughoutBroadcaster::stop() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!running.load()) {
        SPDLOG_INFO("全程播报没有在运行, 无需停止");
        return;
    }
    {
        std::lock_guard<std::mutex> wait_lock(cv_m);
        running.store(false);
    }
    cv.notify_all();
    if (worker.joinable()) worker.join();
    text.clear();
    gap_ms = 1000;
    SPDLOG_INFO("停止全程播报");
}

void ThroughoutBroadcaster::worker_loop() {
    while (running.load()) {
        nlohmann::json params = {{"command", "broadcast-single-text"}, {"content", text}};
        socket.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("全程语音播报: {}, {}", params.dump(), gap_ms);
        sleeping(std::chrono::milliseconds(gap_ms));
    }
}

std::string ThroughoutBroadcaster::get_broadcast_text() {
    return text;
}

int ThroughoutBroadcaster::get_broadcast_gap() {
    return gap_ms;
}

bool ThroughoutBroadcaster::is_running() const {
    return running.load();
}

void ThroughoutBroadcaster::sleeping(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, duration, [this]() {return !running.load();});
}