#include <cmath>
#include <limits>
#include <algorithm>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "state_manager/state_manager.hpp"
#include "broadcast_template.hpp"
#include "obstacle_monitor.hpp"

ObstacleMonitor::ObstacleMonitor() : last_broadcast_time_(std::chrono::steady_clock::now() - std::chrono::seconds(broadcast_cooldown_sec_)) {
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
    // dealer / rep相关
    int timeout_for_req_rep = 5000;
    int linger = 0;
    socket_req_task_center.setsockopt(ZMQ_SNDTIMEO, &timeout_for_req_rep, sizeof(timeout_for_req_rep));
    socket_req_task_center.setsockopt(ZMQ_RCVTIMEO, &timeout_for_req_rep, sizeof(timeout_for_req_rep));
    socket_req_task_center.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_req_task_center.connect("ipc://@task_center_req_rep");
}

ObstacleMonitor::~ObstacleMonitor() {
    stop();
}

void ObstacleMonitor::start() {
    if (running) {
        SPDLOG_WARN("障碍物监控线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&ObstacleMonitor::monitoring, this);
}

void ObstacleMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("障碍物监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void ObstacleMonitor::monitoring() {
    while (running) {
        try {
            using namespace nlohmann;

            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));
            auto now = std::chrono::steady_clock::now();

            // 获取激光障碍物数据
            auto laser_raw_result = http.Get("/gs-robot/real_time_data/laser_raw");
            if (!laser_raw_result || laser_raw_result->status != 200) {
                SPDLOG_WARN("获取激光障碍物数据失败");
                obstacle_active_ = false;
                continue;
            }
            if (laser_raw_result->body.empty()) {
                SPDLOG_WARN("获取到激光障碍物数据为空");
                obstacle_active_ = false;
                continue;
            }

            // 判断有无障碍物
            bool has_obstacle = check_has_obstacle(laser_raw_result->body, max_distance_);

            if (!has_obstacle) {
                // 障碍物消失，重置计时
                obstacle_active_ = false;
                continue;
            }

            // 障碍物出现：首次出现时记录起始时间
            if (!obstacle_active_) {
                obstacle_active_ = true;
                obstacle_since_ = now;
            }

            // 持续时间未达阈值，继续等待
            auto persist = std::chrono::duration_cast<std::chrono::seconds>(now - obstacle_since_);
            if (persist.count() < obstacle_persist_sec_) {
                continue;
            }

            // 播报冷却未到，跳过
            auto since_last_broadcast = std::chrono::duration_cast<std::chrono::seconds>(now - last_broadcast_time_);
            if (since_last_broadcast.count() < broadcast_cooldown_sec_) {
                continue;
            }

            // 判断是否手动模式
            auto global_state_json = GlobalStateManager::getInstance().get_global_state().to_json();
            if (global_state_json.at("mode").get<std::string>() == "manual") {
                SPDLOG_DEBUG("手动模式，不广播");
                continue;
            }
            // 判断是否在充电
            if (global_state_json.at("is_charging").get<bool>() == true) {
                SPDLOG_DEBUG("充电状态，不广播");
                continue;
            }
            // uuid 请求。失败返回空串，此处决定：拿不到 uuid 就不发广播
            auto uuid = request_task_uuid();
            if (uuid.empty()) {
                SPDLOG_DEBUG("没在任务，不广播");
                continue;
            }

            // 播报
            BroadcastTemplate broadcast_template{"lidar.obstacle_at_front"};
            json params = {
                {"command", "broadcast-single-text"},
                {"content", broadcast_template.get_content("请让一让")}
            };
            socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);

            // 更新播报时间
            last_broadcast_time_ = now;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("监控导航激光数据异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

static void replace_all(std::string& s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
}

bool ObstacleMonitor::check_has_obstacle(const std::string& laser_raw_body, double max_distance) {
    using json = nlohmann::json;

    std::string body = laser_raw_body;

    // 激光接口可能用 1e+9999 表示无效距离，nlohmann::json 会数字溢出
    replace_all(body, "1e+9999", "null");
    replace_all(body, "1E+9999", "null");

    auto scan = json::parse(body);

    const double angle_min = scan.at("angle_min").get<double>();
    const double angle_increment = scan.at("angle_increment").get<double>();
    const double range_min = scan.at("range_min").get<double>();
    const double range_max = scan.at("range_max").get<double>();
    const auto& ranges = scan.at("ranges");

    constexpr double pi = 3.14159265358979323846;
    const double front_angle_deg = 15.0;
    const double front_rad = front_angle_deg * pi / 180.0;

    int start_index = static_cast<int>(std::ceil((-front_rad - angle_min) / angle_increment));
    int end_index = static_cast<int>(std::floor((front_rad - angle_min) / angle_increment));

    start_index = std::max(0, start_index);
    end_index = std::min(static_cast<int>(ranges.size()) - 1, end_index);

    int hit_count = 0;

    for (int i = start_index; i <= end_index; ++i) {
        if (!ranges[i].is_number()) continue;

        double distance = ranges[i].get<double>();

        if (!std::isfinite(distance)) continue;
        if (distance < range_min || distance > range_max) continue;

        if (distance <= max_distance) {
            ++hit_count;
        }
    }

    return hit_count >= 3;
}

std::string ObstacleMonitor::request_task_uuid() {
    nlohmann::json params = {{"command", "patrol_duty_uuid"}};

    // DEALER 对接 REP：需要 [空帧][数据帧]
    auto empty_sent = socket_req_task_center.send(zmq::message_t{}, zmq::send_flags::sndmore);
    if (!empty_sent) {
        SPDLOG_ERROR("发送uuid请求失败(空帧)");
        return "";
    }
    auto send_result = socket_req_task_center.send(zmq::buffer(params.dump()), zmq::send_flags::none);
    if (!send_result) {
        SPDLOG_ERROR("发送uuid请求失败");
        return "";
    }

    // 读回：[空帧][数据帧]
    zmq::message_t delim;
    auto d = socket_req_task_center.recv(delim, zmq::recv_flags::none);
    if (!d) {
        SPDLOG_ERROR("接收uuid响应超时(空帧)");
        return "";
    }
    zmq::message_t reply;
    auto recv_result = socket_req_task_center.recv(reply, zmq::recv_flags::none);
    if (!recv_result) {
        SPDLOG_ERROR("接收uuid响应超时");
        return "";
    }

    try {
        auto rep_result = nlohmann::json::parse(std::string_view(static_cast<const char*>(reply.data()), reply.size()));
        std::string uuid = rep_result.at("uuid").get<std::string>();
        SPDLOG_INFO("uuid: {}", uuid);
        return uuid;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("解析uuid响应失败: {}", e.what());
        return "";
    }
}