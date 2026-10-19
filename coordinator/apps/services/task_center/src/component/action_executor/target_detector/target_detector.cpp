#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "../screenshotter.hpp"
#include "target_detector.hpp"

TargetDetector::TargetDetector() {
    rebuild_perceiver_socket();
}

TargetDetector::~TargetDetector() {
    try {
        stop();
    } catch (std::exception &e) {
        SPDLOG_ERROR("目标检测器析构异常: {}", e.what());
    }
}

TargetDetector& TargetDetector::instance() {
    static TargetDetector instance;
    return instance;
}

// action_value3是哪个摄像头, action_value1是哪个检测算法, action_remark是额外参数
void TargetDetector::start(const ActionExecution& action, const std::string& point_name, const std::string& report_uuid) {    
    running.store(true);

    if (action.action_value1 == "1") {
        const int wait_ms = action.action_wait.empty() ? 0 : std::max(0, std::stoi(action.action_wait));
        SPDLOG_INFO("禁区闯入检测等待参数: action_wait='{}', wait_ms={}", action.action_wait, wait_ms);
        std::string direction = action.action_value3 == "1" ? "left" : "right";
        breaking_into_penalty_area(direction, "on");
        sleeping(std::chrono::milliseconds{wait_ms});
        breaking_into_penalty_area(direction, "off");
        return;
    }
    Screenshotter screenshotter;
    screenshotter.execute_snapshot(action, point_name, report_uuid);
}

void TargetDetector::stop() {
    if (!running.load()) return;
    running.store(false);
    wake.notify_all();
}

void TargetDetector::rebuild_perceiver_socket() {
    socket_to_perceiver.close();
    socket_to_perceiver = zmq::socket_t(ctx, zmq::socket_type::req);
    int timeout = kZmqTimeoutMs;
    int linger = kLingerMs;
    int sndhwm = kSndHwm;
    socket_to_perceiver.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_to_perceiver.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket_to_perceiver.connect(kPerceiverEndpoint);
}

// 禁区闯入检测
void TargetDetector::breaking_into_penalty_area(std::string direction, std::string control) {
    nlohmann::json payload = {
        {"cmd", "detection_dynamic_control"},
        {"type", "breaking_into_penalty_area"},
        {"switch", control},
        {"params", direction},
    };
    auto send_result = socket_to_perceiver.send(
        zmq::buffer(payload.dump()), 
        zmq::send_flags::dontwait
    );
    if (!send_result) {
        SPDLOG_WARN("通知检测中心发送失败,重建socket: {}", payload.dump());
        rebuild_perceiver_socket();
        return;
    }
    zmq::message_t reply;
    if (!socket_to_perceiver.recv(reply, zmq::recv_flags::none)) {
        SPDLOG_WARN("检测中心回复超时,重建socket");
        rebuild_perceiver_socket();
        return;
    }
    SPDLOG_INFO("检测中心确认: {}", reply.to_string_view());
}

// 门异常打开检测
void TargetDetector::door_opened_abnormally(std::string direction, std::string control) {

}

void TargetDetector::sleeping(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(wake_mutex);
    wake.wait_for(lock, duration, [this] {return !running.load();});
}

bool TargetDetector::is_running() const {
    return running.load();
}