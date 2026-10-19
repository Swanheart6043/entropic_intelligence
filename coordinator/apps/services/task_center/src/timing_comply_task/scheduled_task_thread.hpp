#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <zmq.hpp>
#include "component/mcu_controller.hpp"

struct ScheduledTask {
    int id;
    std::string type;
    std::string value;
};

class ScheduledTaskThread {
public:
    ScheduledTaskThread();
    ~ScheduledTaskThread();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::mutex lifecycle_mutex;
    std::mutex cv_m;
    std::condition_variable cv;

    zmq::context_t ctx{1};
    zmq::socket_t socket_publisher{ctx, zmq::socket_type::pub};
    std::vector<ScheduledTask> scheduled_task_list;
    McuController mcu_controller;

    void monitoring_loop();
    void query_scheduled_task();
    void executor_scheduled_task();
    bool mark_task_started(int task_id);
    void handle_volume(const std::string& volume);
    void handle_head_light(const std::string& switch_flag);
    void handle_warning_light(const std::string& switch_flag);
    void sleeping(std::chrono::milliseconds duration);
};
