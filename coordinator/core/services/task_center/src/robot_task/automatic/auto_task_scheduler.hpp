#pragma once

#include <atomic>
#include <optional>
#include <string>
#include <zmq.hpp>

#include "config_parser.hpp"
#include "./common/task_run_outcome.hpp"

enum class AutoTaskKind {
    NONE,
    PATROL,
    DUTY,
};

struct AutoTaskRunResult {
    bool handled = false;
    bool conflict = false;
    AutoTaskKind kind = AutoTaskKind::NONE;
    TaskRunOutcome outcome = TaskRunOutcome::ERROR;
};

// 只负责自动计划的选择与执行，不负责充电、手动任务或全局运动仲裁。
// 所有公开方法都由 RobotTaskCoordinator 的 worker 线程调用，cancel 除外。
class AutoTaskScheduler {
public:
    AutoTaskScheduler();
    AutoTaskScheduler(const AutoTaskScheduler&) = delete;
    AutoTaskScheduler& operator=(const AutoTaskScheduler&) = delete;

    AutoTaskRunResult resume_snapshot();
    AutoTaskRunResult dispatch_once();
    void prepare_dispatch();

    // 可由协调器以外的控制线程调用；子状态机内部使用原子取消标志。
    void cancel();

    // 以下方法只在当前自动任务完全退出后调用。
    void restart_last_task();
    void forget_execution_marks();

    AutoTaskKind active_task() const;
    std::string get_task_uuid();

private:
    std::atomic<AutoTaskKind> active_task_{AutoTaskKind::NONE};
    std::atomic<AutoTaskKind> last_task_{AutoTaskKind::NONE};
    std::atomic<bool> cancel_requested_{false};
    bool simultaneous_reported_ = false;

    Config* config_ = Config::getInstance();
    zmq::context_t ctx_{1};
    zmq::socket_t socket_to_web_{ctx_, zmq::socket_type::push};

    AutoTaskRunResult run_patrol();
    AutoTaskRunResult run_duty();
    void report_simultaneous_patrol_and_duty();
};
