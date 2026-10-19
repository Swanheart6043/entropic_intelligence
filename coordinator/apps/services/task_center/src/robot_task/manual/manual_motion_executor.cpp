#include <ctime>
#include <exception>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "manual_motion_executor.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "robot_task/task_ownership.hpp"

ManualMotionExecutor& ManualMotionExecutor::instance() {
    static ManualMotionExecutor inst;
    return inst;
}

ManualMotionExecutor::ManualMotionExecutor() {
    http_ = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
    socket_to_web_.connect("tcp://127.0.0.1:2007");
    socket_to_edge_.connect("ipc://@edge_gateway_sub_task_center");
}

ManualMotionExecutor::~ManualMotionExecutor() {
    stop();
}

void ManualMotionExecutor::bind_ownership(ManualMotionOwnership& ownership) {
    ownership_ = &ownership;
}

void ManualMotionExecutor::start() {
    if (running_.exchange(true)) {
        SPDLOG_WARN("手动运动执行器已经在运行");
        return;
    }
    worker_ = std::thread(&ManualMotionExecutor::worker_loop, this);
    SPDLOG_INFO("手动运动执行器启动");
}

void ManualMotionExecutor::stop() {
    if (!running_.exchange(false)) return;
    interrupt_current(); // 打断可能在跑的任务 + 唤醒 worker 退出
    if (worker_.joinable()) worker_.join();
    SPDLOG_INFO("手动运动执行器已停止");
}

// ============ 生产者接口(用户交互线程调用) ============
void ManualMotionExecutor::submit(Task task) {
    if (!ownership_) {
        SPDLOG_ERROR("手动运动执行器尚未绑定任务所有权协调器");
        return;
    }
    if (!ownership_->prepare_manual_motion()) {
        SPDLOG_WARN("当前模式不允许提交手动运动任务");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_);
        pending_ = std::move(task); // 单槽最新覆盖
    }
    if (busy_.load()) {
        SPDLOG_INFO("有任务在跑, 抢占: 取消当前 → 跑新的");
        interrupt_current();        // 打断当前, worker 善后后自然取走 pending_
    } else {
        cv_.notify_all();           // 唤醒空闲 worker
    }
}

void ManualMotionExecutor::cancel_current() {
    {
        std::lock_guard<std::mutex> lock(m_);
        pending_.reset();
    }
    if (busy_.load()) {
        SPDLOG_INFO("取消当前手动运动任务");
        interrupt_current();
    }
}

void ManualMotionExecutor::pause_current() {
    if (!ownership_ || ownership_->current_state() != RobotTaskState::RUNNING_MANUAL_MOTION) {
        SPDLOG_WARN("当前运动不属于手动执行器，忽略手动暂停命令");
        return;
    }
    // 暂停是 robot 侧命令, 不打断 worker 的导航等待(导航保持, resume 可继续)
    Navigator::instance().pause_navigation();
}

void ManualMotionExecutor::resume_current() {
    if (!ownership_ || ownership_->current_state() != RobotTaskState::RUNNING_MANUAL_MOTION) {
        SPDLOG_WARN("当前运动不属于手动执行器，忽略手动恢复命令");
        return;
    }
    Navigator::instance().resume_navigation();
}

// ============ worker 线程 ============
void ManualMotionExecutor::worker_loop() {
    while (running_.load()) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [this] { return pending_.has_value() || !running_.load(); });
            if (!running_.load()) break;
            task = std::move(*pending_);
            pending_.reset();
            cancel_flag_.store(false); // 新任务开始, 清取消标志
            busy_.store(true);
        }
        if (!ownership_ || !ownership_->acquire_manual_motion(running_, cancel_flag_)) {
            busy_.store(false);
            continue;
        }

        NavigationResult result = NavigationResult::FAILED;
        try {
            result = execute(task);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("手动运动任务异常: {}", e.what());
        }
        ownership_->release_manual_motion();
        SPDLOG_INFO("手动运动任务结束: kind={}, result={}", static_cast<int>(task.kind), static_cast<int>(result));

        busy_.store(false);
        // 执行期间若来了新任务(抢占), 下一轮循环自然取走
    }
    SPDLOG_INFO("手动运动执行器线程退出");
}

NavigationResult ManualMotionExecutor::execute(const Task& task) {
    // 任务开始前若已被取消(极窄竞态窗口), 直接跳过, 不下发运动
    if (cancel_flag_.load()) return NavigationResult::CANCELLED;

    switch (task.kind) {
        case Kind::MOVE:            return run_move(task);
        case Kind::GO_TO_POINT:     return run_go_to_point(task);
        case Kind::RUN_LINE:        return run_line(task);
        case Kind::INITIALIZE_WAIT: return run_initialize_wait(task);
    }
    return NavigationResult::FAILED;
}

NavigationResult ManualMotionExecutor::run_move(const Task& task) {
    nlohmann::json params = {{"speed", {
        {"linearSpeed", task.linear_speed},
        {"angularSpeed", task.angular_speed}
    }}};
    auto response = http_->Post(base_url + "/cmd/move", params.dump(), "application/json");
    if (!response || response->status != 200) {
        SPDLOG_ERROR("Move失败: {}", params.dump());
        return NavigationResult::FAILED;
    }
    SPDLOG_INFO("Move完成");
    return NavigationResult::SUCCESS;
}

NavigationResult ManualMotionExecutor::run_go_to_point(const Task& task) {
    auto result = Navigator::instance().nav_to_custom_point(
        task.map_name,
        static_cast<int>(task.x),
        static_cast<int>(task.y),
        static_cast<int>(task.angle));
    SPDLOG_INFO("导航到点结果: {}", static_cast<int>(result));

    if (result == NavigationResult::CANCELLED) {
        Navigator::instance().cancel_navigation();  // 抢占/取消: 真正停下机器人
    } else if (result == NavigationResult::SUCCESS) {
        notify_arrival_custom_point(task);
    }
    return result;
}

NavigationResult ManualMotionExecutor::run_line(const Task& task) {
    try {
        // worker 线程内拉取线上的点(不阻塞收命令线程)
        // 注意: map_name 用纯字符串拼 URL(旧实现用 json.dump() 会带引号, 已修正)
        auto resp = http_->Get(base_url + "/data/positions?map_name=" + task.map_name + "&type=2");
        if (!resp || resp->status != 200) {
            SPDLOG_ERROR("跑线: 拉取点位失败");
            return NavigationResult::FAILED;
        }
        auto point_list = nlohmann::json::parse(resp->body).at("data");
        if (point_list.empty()) {
            SPDLOG_WARN("跑线: 点位为空");
            return NavigationResult::SKIPPED;
        }

        for (const auto& item : point_list) {
            if (cancel_flag_.load()) {          // 点与点之间检查抢占/取消
                Navigator::instance().cancel_navigation();
                return NavigationResult::CANCELLED;
            }
            auto point_name = item.at("name").get<std::string>();
            auto r = Navigator::instance().nav_to_normal_point(task.map_name, point_name);
            if (r == NavigationResult::CANCELLED) {  // 单点导航中途被打断
                Navigator::instance().cancel_navigation();
                return NavigationResult::CANCELLED;
            }
            // 其余结果(SKIPPED / FAILED / TIMEOUT ...): 这里选择跳过该点、继续下一个
        }
        SPDLOG_INFO("跑线完成");
        return NavigationResult::SUCCESS;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("跑线异常: {}", e.what());
        return NavigationResult::FAILED;
    }
}

NavigationResult ManualMotionExecutor::run_initialize_wait(const Task& task) {
    try {
        nlohmann::json params = {
            {"mapName", task.map_name},
            {"point", {
                {"angle", task.angle},
                {"gridPosition", {{"x", task.x}, {"y", task.y}}}
            }}
        };
        auto resp = http_->Post(base_url + "/cmd/initialize_customized", params.dump(), "application/json");
        bool ok = resp && resp->status == 200;
        report_init_result(ok);  // 与旧实现一致: 下发成功即回 RC_INIT_RET "6"
        if (!ok) {
            SPDLOG_ERROR("转圈初始化下发失败: {}", params.dump());
            return NavigationResult::FAILED;
        }

        // 占住"运动位", 轮询直到转圈完成 / 被取消 / 被抢占
        while (running_.load()) {
            if (!interruptible_wait(std::chrono::seconds(1))) {
                http_->Get(base_url + "/cmd/stop_initialize");  // 被中止 → 通知 robot 停止初始化
                SPDLOG_INFO("转圈初始化被中止");
                return NavigationResult::CANCELLED;
            }
            if (initialize_finished()) {
                SPDLOG_INFO("转圈初始化完成");
                return NavigationResult::SUCCESS;
            }
        }
        return NavigationResult::CANCELLED;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("转圈初始化异常: {}", e.what());
        return NavigationResult::FAILED;
    }
}

// ============ 辅助 ============

void ManualMotionExecutor::interrupt_current() {
    cancel_flag_.store(true);
    Navigator::instance().sync_control(true, false); // 唤醒阻塞中的导航等待 → 返回 CANCELLED
    cv_.notify_all();                                // 唤醒 init 轮询 / 空闲等待
}

bool ManualMotionExecutor::interruptible_wait(std::chrono::milliseconds d) {
    std::unique_lock<std::mutex> lock(m_);
    cv_.wait_for(lock, d, [this] {
        return cancel_flag_.load() || !running_.load() || pending_.has_value();
    });
    return running_.load() && !cancel_flag_.load() && !pending_.has_value();
}

void ManualMotionExecutor::notify_arrival_custom_point(const Task& task) {
    nlohmann::json msg = {
        {"topic", "arrival_nav_custom_point"},
        {"x", task.x_text.empty() ? std::to_string(task.x) : task.x_text},
        {"y", task.y_text.empty() ? std::to_string(task.y) : task.y_text},
        {"angle", task.angle_text.empty() ? std::to_string(task.angle) : task.angle_text}
    };
    socket_to_edge_.send(zmq::buffer(msg.dump()), zmq::send_flags::dontwait);
}

void ManualMotionExecutor::report_init_result(bool ok) {
    nlohmann::json msg = {
        {"Header", {
            {"CmdType", "RC_INIT_RET"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ok ? "6" : "-1"}
    };
    socket_to_web_.send(zmq::buffer(msg.dump()), zmq::send_flags::dontwait);
}

// ⚠️ 按你机器人 API /cmd/is_initialize_finished 的真实返回调整。
// 这里假设返回体形如 {"data": true} 表示转圈完成 —— 若字段名/结构不同, 改这里即可。
bool ManualMotionExecutor::initialize_finished() {
    auto resp = http_->Get(base_url + "/cmd/is_initialize_finished");
    if (!resp || resp->status != 200) return false;
    try {
        return nlohmann::json::parse(resp->body).value("data", false);
    } catch (...) {
        return false;
    }
}
