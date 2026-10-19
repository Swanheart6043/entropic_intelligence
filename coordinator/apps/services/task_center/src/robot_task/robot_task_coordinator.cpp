#include <exception>
#include <spdlog/spdlog.h>
#include "robot_task_coordinator.hpp"
#include "mode/mode_service.hpp"
#include "robot_task/charging/charging_executor.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "robot_task/manual/manual_motion_executor.hpp"

RobotTaskCoordinator& RobotTaskCoordinator::instance() {
    static RobotTaskCoordinator instance;
    return instance;
}

RobotTaskCoordinator::RobotTaskCoordinator() {
    mode_.store(ModeService::instance().get_current_mode());
    ModeService::instance().register_observer(this);
}

RobotTaskCoordinator::~RobotTaskCoordinator() {
    ModeService::instance().unregister_observer(this);
    stop();
}

void RobotTaskCoordinator::start() {
    if (running_.exchange(true)) {
        SPDLOG_WARN("机器人任务协调器已经在运行");
        return;
    }
    ManualMotionExecutor::instance().bind_ownership(*this);
    worker_ = std::thread(&RobotTaskCoordinator::worker_loop, this);
    SPDLOG_INFO("机器人任务协调器启动");
}

void RobotTaskCoordinator::stop() {
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        charging_request_.reset();
        charging_should_restart_auto_ = false;
    }
    const auto state = state_.load();
    if (state == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    } else if (state == RobotTaskState::RUNNING_CHARGING) {
        cancel_charging_session();
    } else if (state == RobotTaskState::RUNNING_MANUAL_MOTION || state == RobotTaskState::RUNNING_INTERNAL_MOTION) {
        Navigator::instance().sync_control(true, false);
        (void)Navigator::instance().cancel_navigation();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
    set_state(RobotTaskState::IDLE);
    SPDLOG_INFO("机器人任务协调器已停止");
}

void RobotTaskCoordinator::worker_loop() {
    while (running_.load()) {
        try {
            if (run_pending_charging()) continue;

            bool do_forget = false;
            bool do_restart = false;
            bool do_resume = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                do_forget = forget_requested_;
                forget_requested_ = false;
                do_restart = restart_requested_;
                restart_requested_ = false;
                do_resume = resume_requested_;
                resume_requested_ = false;
            }

            if (do_forget) auto_scheduler_.forget_execution_marks();
            if (do_restart) auto_scheduler_.restart_last_task();

            if (mode_.load() == RobotMode::MANUAL) {
                // 手动执行器独立工作；协调器只保留所有权仲裁和充电请求处理。
                if (do_resume) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    resume_requested_ = true;
                }
                wait_while_manual();
                continue;
            }

            auto result = run_auto_once(do_resume);
            handle_auto_result(result);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("机器人任务协调器执行异常: {}", e.what());
            set_state(RobotTaskState::IDLE);
            sleeping(std::chrono::seconds(1));
        } catch (...) {
            SPDLOG_ERROR("机器人任务协调器发生未知异常");
            set_state(RobotTaskState::IDLE);
            sleeping(std::chrono::seconds(1));
        }
    }
    SPDLOG_INFO("机器人任务协调器线程退出");
}

void RobotTaskCoordinator::change_mode(RobotMode new_mode, std::optional<AutoModeOperation> operation) {
    mode_.store(new_mode);

    if (new_mode == RobotMode::MANUAL) {
        SPDLOG_INFO("切换到手动模式，释放自动任务所有权");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            resume_requested_ = false;
            restart_requested_ = false;
        }
        cancel();
        wake_.notify_all();
        return;
    }

    // 自动模式启用时，先停止可能仍在执行的手动运动。若正在充电，恢复请求会延后处理。
    ManualMotionExecutor::instance().cancel_current();
    if (operation == AutoModeOperation::RESTART) {
        SPDLOG_INFO("切换到自动模式：待当前任务释放后从头调度");
        restart();
    } else {
        SPDLOG_INFO("切换到自动模式：待当前任务释放后恢复快照");
        recover();
    }
}

void RobotTaskCoordinator::cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 尚未获得所有权的充电请求也属于可取消任务。
        charging_request_.reset();
        charging_should_restart_auto_ = false;
    }
    // 同时清掉尚未取得所有权的手动单槽任务。
    ManualMotionExecutor::instance().cancel_current();
    const auto state = state_.load();
    switch (state) {
        case RobotTaskState::RUNNING_AUTO_TASK: {
            auto_scheduler_.cancel();
            break;
        }
        case RobotTaskState::RUNNING_CHARGING: {
            cancel_charging_session();
            break;
        }
        case RobotTaskState::RUNNING_MANUAL_MOTION: {
            break;
        }
        case RobotTaskState::RUNNING_INTERNAL_MOTION: {
            Navigator::instance().sync_control(true, false);
            (void)Navigator::instance().cancel_navigation();
            break;
        }
        case RobotTaskState::IDLE: {
            break;
        }
    }
    wake_.notify_all();
}

void RobotTaskCoordinator::cancel_auto_task() {
    if (state_.load() == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    }
    wake_.notify_all();
}

void RobotTaskCoordinator::recover() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        resume_requested_ = true;
        restart_requested_ = false;
    }
    wake_.notify_all();
}

void RobotTaskCoordinator::restart() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        restart_requested_ = true;
        resume_requested_ = false;
    }
    if (state_.load() == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    }
    wake_.notify_all();
}

void RobotTaskCoordinator::forget() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        forget_requested_ = true;
    }
    wake_.notify_all();
}

AutoTaskRunResult RobotTaskCoordinator::run_auto_once(bool resume_snapshot) {
    auto_scheduler_.prepare_dispatch();
    {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] {
            return (
                !running_.load() ||
                mode_.load() != RobotMode::AUTO ||
                charging_request_.has_value() ||
                resume_requested_ || 
                restart_requested_ || 
                forget_requested_ ||
                state_.load() == RobotTaskState::IDLE
            );
        });
        const bool newer_control_request = resume_requested_ || restart_requested_ || forget_requested_;
        if (!running_.load() || mode_.load() != RobotMode::AUTO || charging_request_ || newer_control_request) {
            // 当前这次恢复只在没有更新控制意图时保留；restart/forget/新的 recover 已经能够决定下一轮行为，避免旧恢复请求反向覆盖。
            if (resume_snapshot && !newer_control_request) resume_requested_ = true;
            return {};
        }
        state_.store(RobotTaskState::RUNNING_AUTO_TASK);
    }
    wake_.notify_all();

    AutoTaskRunResult result;
    if (resume_snapshot) {
        result = auto_scheduler_.resume_snapshot();
    }
    if (!result.handled && !result.conflict && running_.load()) {
        result = auto_scheduler_.dispatch_once();
    }
    set_state(RobotTaskState::IDLE);
    return result;
}

void RobotTaskCoordinator::handle_auto_result(const AutoTaskRunResult& result) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (charging_request_ && charging_should_restart_auto_) {
            // RUNNING_AUTO_TASK 也覆盖“正在查询任务”的短暂窗口；只有状态机
            // 确实因取消退出，才算本次充电打断了一个自动任务。
            charging_should_restart_auto_ = result.handled && result.outcome == TaskRunOutcome::CANCELLED;
        }
    }

    if (result.handled && result.outcome == TaskRunOutcome::COMPLETED) {
        const auto reason = result.kind == AutoTaskKind::PATROL ? ChargingReason::PATROL_COMPLETED : ChargingReason::DUTY_COMPLETED;
        request_charging(reason);
        return;
    }
    if (!result.handled || result.conflict || result.outcome == TaskRunOutcome::NOT_STARTED) {
        sleeping(std::chrono::seconds(5));
    }
}

void RobotTaskCoordinator::request_charging(ChargingReason reason) {
    RobotTaskState state = RobotTaskState::IDLE;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = state_.load();
        const bool automatic_completion = reason == ChargingReason::PATROL_COMPLETED || reason == ChargingReason::DUTY_COMPLETED;
        if (automatic_completion && mode_.load() != RobotMode::AUTO) {
            SPDLOG_INFO("已切换到手动模式，忽略自动任务完成后的充电请求");
            return;
        }
        if (state_.load() == RobotTaskState::RUNNING_CHARGING || charging_request_) {
            SPDLOG_INFO("充电任务已经在执行或等待中，忽略重复请求：{}", charging_reason_name(reason));
            return;
        }
        charging_request_ = reason;
        // 只有确实打断了自动任务的充电，完成后才从头恢复该任务。巡逻/值守正常完成后发起的充电不能清除当天的执行标记。
        charging_should_restart_auto_ = mode_.load() == RobotMode::AUTO && state == RobotTaskState::RUNNING_AUTO_TASK && !automatic_completion;
        charging_complete_ = false;
        charging_cancel_requested_ = false;
    }

    SPDLOG_INFO("收到充电任务请求：{}", charging_reason_name(reason));

    // 先提交取消意图。真正的充电导航只会在旧任务释放所有权后由 worker 启动。
    if (state == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    } else if (state == RobotTaskState::RUNNING_MANUAL_MOTION) {
        ManualMotionExecutor::instance().cancel_current();
    }
    wake_.notify_all();
}

void RobotTaskCoordinator::cancel_charging_session() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        charging_cancel_requested_ = true;
    }
    ChargingExecutor::instance().cancel_charging();
    wake_.notify_all();
}

void RobotTaskCoordinator::notify_charging_complete() {
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_.load() == RobotTaskState::RUNNING_CHARGING && ChargingExecutor::instance().phase() == ChargingPhase::DOCKED) {
            charging_complete_ = true;
            accepted = true;
        }
    }
    if (!accepted) {
        SPDLOG_INFO("当前不在桩上充电，忽略无效或重复的充电完成事件");
        return;
    }
    SPDLOG_INFO("收到充电完成事件");
    wake_.notify_all();
}

bool RobotTaskCoordinator::handle_plan_updated() {
    RobotTaskState state = RobotTaskState::IDLE;
    ChargingPhase charging_phase = ChargingPhase::IDLE;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = state_.load();
        if (state == RobotTaskState::RUNNING_CHARGING) {
            charging_phase = ChargingExecutor::instance().phase();
            if (charging_phase == ChargingPhase::DOCKING) {
                SPDLOG_INFO("计划已更新，但机器人正在对桩，本次更新不打断充电流程");
                return false;
            }
        }

        // 计划更新优先于尚未开始或正在进行的充电任务。
        charging_request_.reset();
        charging_should_restart_auto_ = false;
        forget_requested_ = true;
        restart_requested_ = true;
        resume_requested_ = false;
        if (state == RobotTaskState::RUNNING_CHARGING &&
                charging_phase == ChargingPhase::DOCKED) {
            charging_cancel_requested_ = true;
        }
    }

    if (state == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    } else if (state == RobotTaskState::RUNNING_CHARGING) {
        if (charging_phase == ChargingPhase::DOCKED) {
            SPDLOG_INFO("计划已更新，退出充电并从头调度自动任务");
            // 已经在桩上时没有充电导航需要取消；释放所有权后让新的
            // 导航请求自行完成安全退桩/离桩。
        } else {
            SPDLOG_INFO("计划已更新，取消前往充电点并从头调度自动任务");
            cancel_charging_session();
        }
    }

    wake_.notify_all();
    return true;
}

bool RobotTaskCoordinator::prepare_manual_motion() {
    if (mode_.load() != RobotMode::MANUAL) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 明确的手动运动采用抢占语义，不让尚未开始的充电请求随后反向抢占。
        charging_request_.reset();
        charging_should_restart_auto_ = false;
    }
    const auto state = state_.load();
    if (state == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    } else if (state == RobotTaskState::RUNNING_CHARGING) {
        cancel_charging_session();
    }
    wake_.notify_all();
    return true;
}

bool RobotTaskCoordinator::acquire_manual_motion(const std::atomic<bool>& requester_running, const std::atomic<bool>& requester_cancelled) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load() && requester_running.load() && !requester_cancelled.load()) {
        if (mode_.load() != RobotMode::MANUAL) return false;
        if (state_.load() == RobotTaskState::IDLE && !charging_request_.has_value()) break;
        wake_.wait_for(lock, std::chrono::milliseconds(100));
    }

    if (!running_.load() || !requester_running.load() || requester_cancelled.load() ||
            mode_.load() != RobotMode::MANUAL ||
            state_.load() != RobotTaskState::IDLE || charging_request_.has_value()) {
        return false;
    }
    state_.store(RobotTaskState::RUNNING_MANUAL_MOTION);
    SPDLOG_INFO("手动运动获得机器人任务所有权");
    return true;
}

void RobotTaskCoordinator::release_manual_motion() {
    if (state_.load() != RobotTaskState::RUNNING_MANUAL_MOTION) return;
    set_state(RobotTaskState::IDLE);
    SPDLOG_INFO("手动运动释放机器人任务所有权");
}

void RobotTaskCoordinator::prepare_internal_motion() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        charging_request_.reset();
        charging_should_restart_auto_ = false;
    }

    const auto state = state_.load();
    if (state == RobotTaskState::RUNNING_AUTO_TASK) {
        auto_scheduler_.cancel();
    } else if (state == RobotTaskState::RUNNING_CHARGING) {
        cancel_charging_session();
    } else if (state == RobotTaskState::RUNNING_MANUAL_MOTION) {
        ManualMotionExecutor::instance().cancel_current();
    }
    wake_.notify_all();
}

bool RobotTaskCoordinator::acquire_internal_motion(const std::atomic<bool>& requester_running) {
    std::unique_lock<std::mutex> lock(mutex_);
    while (running_.load() && requester_running.load()) {
        if (state_.load() == RobotTaskState::IDLE && !charging_request_) break;
        wake_.wait_for(lock, std::chrono::milliseconds(100));
    }
    if (!running_.load() || !requester_running.load() ||
            state_.load() != RobotTaskState::IDLE || charging_request_) {
        return false;
    }
    state_.store(RobotTaskState::RUNNING_INTERNAL_MOTION);
    SPDLOG_INFO("内部运动获得机器人任务所有权");
    return true;
}

void RobotTaskCoordinator::release_internal_motion() {
    if (state_.load() != RobotTaskState::RUNNING_INTERNAL_MOTION) return;
    set_state(RobotTaskState::IDLE);
    SPDLOG_INFO("内部运动释放机器人任务所有权");
}

void RobotTaskCoordinator::sleeping(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait_for(lock, duration, [this] {
        const bool rule = (
            !running_.load() ||
            mode_.load() == RobotMode::MANUAL ||
            charging_request_.has_value() ||
            resume_requested_ ||
            restart_requested_ ||
            forget_requested_
        );
        return rule;
    });
}

void RobotTaskCoordinator::wait_while_manual() {
    std::unique_lock<std::mutex> lock(mutex_);
    wake_.wait(lock, [this] {
        const bool rule = (
            !running_.load() ||
            mode_.load() != RobotMode::MANUAL ||
            charging_request_.has_value() ||
            forget_requested_ || 
            restart_requested_
        );
        return rule;
    });
}

bool RobotTaskCoordinator::run_pending_charging() {
    std::optional<ChargingReason> reason;
    bool restart_auto_after_complete = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!charging_request_) return false;

        wake_.wait(lock, [this] {
            return !running_.load() ||
                !charging_request_.has_value() ||
                state_.load() == RobotTaskState::IDLE;
        });
        if (!running_.load()) return true;
        if (!charging_request_) return true;

        reason = charging_request_;
        charging_request_.reset();
        restart_auto_after_complete = charging_should_restart_auto_;
        charging_should_restart_auto_ = false;
        charging_complete_ = false;
        charging_cancel_requested_ = false;
        // 必须在同一临界区内先清上次取消标志、再发布充电状态。发布后到来的取消方会看到 RUNNING_CHARGING，并重新置位取消标志。
        ChargingExecutor::instance().prepare_run();
        state_.store(RobotTaskState::RUNNING_CHARGING);
    }
    wake_.notify_all();
    SPDLOG_INFO("充电任务获得机器人任务所有权：{}", charging_reason_name(*reason));

    // 给刚退出的导航接口留出稳定窗口；等待可被取消和停机打断。
    {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait_for(lock, std::chrono::seconds(1), [this] {
            return !running_.load() || charging_cancel_requested_;
        });
    }

    ChargingOutcome outcome = ChargingOutcome::CANCELLED;
    if (running_.load()) {
        bool should_run = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            should_run = !charging_cancel_requested_;
        }
        if (should_run) {
            // run 为阻塞调用，由协调器 worker 独占执行。
            outcome = ChargingExecutor::instance().run(*reason);
        }
    }

    bool completed = false;
    if (outcome == ChargingOutcome::DOCKED) {
        SPDLOG_INFO("已对桩，保持充电任务所有权直到收到充电完成或取消事件");
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] {
            return !running_.load() || charging_complete_ || charging_cancel_requested_;
        });
        // 模式切换、计划更新等取消意图优先于同时到达的充电完成消息。
        completed = charging_complete_ && !charging_cancel_requested_;
    }

    ChargingExecutor::instance().finish_session();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        charging_complete_ = false;
        charging_cancel_requested_ = false;
        if (completed && restart_auto_after_complete) {
            restart_requested_ = true;
            resume_requested_ = false;
        }
        state_.store(RobotTaskState::IDLE);
    }
    wake_.notify_all();
    if (completed && restart_auto_after_complete) {
        SPDLOG_INFO("充电完成，重新开始被充电打断的自动任务");
    }
    SPDLOG_INFO("充电任务释放机器人任务所有权");
    return true;
}

void RobotTaskCoordinator::set_state(RobotTaskState state) {
    state_.store(state);
    wake_.notify_all();
}

RobotTaskState RobotTaskCoordinator::current_state() const {
    return state_.load();
}

std::string RobotTaskCoordinator::get_task_uuid() {
    if (state_.load() != RobotTaskState::RUNNING_AUTO_TASK) return "";
    return auto_scheduler_.get_task_uuid();
}

bool RobotTaskCoordinator::is_idle() const {
    return state_.load() == RobotTaskState::IDLE;
}

bool RobotTaskCoordinator::is_progress() const {
    return state_.load() != RobotTaskState::IDLE;
}
