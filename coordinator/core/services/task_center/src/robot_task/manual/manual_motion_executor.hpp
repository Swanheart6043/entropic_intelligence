#pragma once
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <memory>
#include <chrono>
#include <zmq.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"
#include "component/navigate_executor/navigation_types.hpp"

class ManualMotionOwnership;

// ===================== 手动运动执行器 =====================
// 手动模式下，所有控制机器人运动的任务（速度移动 / 导航到点 / 跑线 / 转圈初始化）
// 都收敛到这里的单一 worker 线程串行执行 —— 天然做到"同一时刻只有一种运动"。
//
// 线程模型约定:
//   - submit / cancel_current / pause_current / resume_current 由用户交互线程调用,
//     只投递意图(翻原子标志 / 发 robot 侧 HTTP 控制命令), 立即返回, 绝不阻塞收命令的线程。
//   - worker_loop 是唯一有权调用 Navigator::nav_to_* 这类阻塞接口、以及 http_ 的线程。
//     因此除 pause/resume 外, 所有停止/初始化 HTTP 都在 worker 线程发, 无跨线程共用 client 的竞态。
//
// busy 语义: 抢占(取消当前 → 跑新的)。submit 覆盖单槽 pending_ 并打断当前任务;
//            不做快照 recover —— 手动一次性任务不需要。
class ManualMotionExecutor {
public:
    enum class Kind { MOVE, GO_TO_POINT, RUN_LINE, INITIALIZE_WAIT };

    struct Task {
        Kind kind = Kind::GO_TO_POINT;
        std::string map_name;
        double x = 0.0;      // GO_TO_POINT / INITIALIZE_WAIT: 栅格坐标 + 朝向
        double y = 0.0;
        double angle = 0.0;
        std::string x_text;
        std::string y_text;
        std::string angle_text;
        double linear_speed = 0.0;
        double angular_speed = 0.0;
    };

    static ManualMotionExecutor& instance();
    ManualMotionExecutor(const ManualMotionExecutor&) = delete;
    ManualMotionExecutor& operator=(const ManualMotionExecutor&) = delete;

    void start();
    void stop();
    void bind_ownership(ManualMotionOwnership& ownership);

    // ===== 生产者接口(用户交互线程调用, 立即返回) =====
    void submit(Task task);   // busy 时抢占: 取消当前 → 跑新的
    void cancel_current();    // 取消当前并清空待执行
    void pause_current();     // 暂停当前导航(robot 侧, 仅对导航类有效)
    void resume_current();    // 恢复当前导航

    // ===== 查询 =====
    bool is_busy() const { return busy_.load(); }

private:
    ManualMotionExecutor();
    ~ManualMotionExecutor();

    void worker_loop();
    NavigationResult execute(const Task& task);
    NavigationResult run_move(const Task& task);
    NavigationResult run_go_to_point(const Task& task);
    NavigationResult run_line(const Task& task);
    NavigationResult run_initialize_wait(const Task& task);

    // 打断当前任务(抢占 / 取消 共用): 置标志 + 唤醒阻塞的导航等待 + 唤醒 worker
    void interrupt_current();
    // 可中断等待: 返回 true=可继续, false=应中止当前任务(被取消/抢占/停机)
    bool interruptible_wait(std::chrono::milliseconds d);

    // 完成通知 / 轮询
    void notify_arrival_custom_point(const Task& task);
    void report_init_result(bool ok);
    bool initialize_finished();

    Config* config = Config::getInstance();
    std::string base_url = "/gs-robot";
    std::unique_ptr<httplib::Client> http_; // 仅 worker 线程使用(跑线拉点 / 初始化)

    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> cancel_flag_{false}; // 抢占/取消置位; worker 在任务边界检查
    std::thread worker_;
    ManualMotionOwnership* ownership_ = nullptr; // start 前由 RobotTaskCoordinator 绑定

    std::mutex m_;
    std::condition_variable cv_;
    std::optional<Task> pending_; // 单槽, 最新覆盖

    zmq::context_t ctx_{1};
    zmq::socket_t socket_to_web_{ctx_, zmq::socket_type::push};  // tcp://127.0.0.1:2007
    zmq::socket_t socket_to_edge_{ctx_, zmq::socket_type::pub};  // ipc://@edge_gateway_sub_task_center
};
