#pragma once
#include <zmq.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"

class ObstacleMonitor {
public:
    explicit ObstacleMonitor();
    ~ObstacleMonitor();
    void start();
    void stop();

private:
    Config* config = Config::getInstance();

    std::atomic<bool> running{false};
    std::thread worker_thread;

    // 轮询周期(毫秒)：多久采一帧激光
    int poll_interval_ms_ = 2000;
    // 障碍物距离阈值(米)：前方多少米内算有障碍物
    double max_distance_ = 1.0;
    // 障碍物持续判定(秒)：连续存在多久才播报
    int obstacle_persist_sec_ = 5;
    // 播报冷却(秒)：两次播报之间的最小间隔
    int broadcast_cooldown_sec_ = 20;

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};
    zmq::socket_t socket_req_task_center{ctx, zmq::socket_type::dealer};

    httplib::Client http = httplib::Client("http://10.10.10.10:8080");

    // 障碍物首次出现的时间点；无障碍物时置为空
    std::chrono::steady_clock::time_point obstacle_since_{};
    bool obstacle_active_ = false;
    // 上次播报时间
    std::chrono::steady_clock::time_point last_broadcast_time_;

    void monitoring();
    bool check_has_obstacle(const std::string& laser_raw_body, double max_distance);
    std::string request_task_uuid();
};