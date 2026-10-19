#include <ctime>
#include <utility>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "auto_task_scheduler.hpp"
#include "./patrol/patrol_state_machine.hpp"
#include "./duty/duty_state_machine.hpp"

AutoTaskScheduler::AutoTaskScheduler() {
    socket_to_web_.connect("tcp://127.0.0.1:2007");
}

AutoTaskRunResult AutoTaskScheduler::resume_snapshot() {
    auto& patrol = PatrolStateMachine::instance();
    if (patrol.resume_patrol_snapshot()) {
        return run_patrol();
    }

    auto& duty = DutyStateMachine::instance();
    if (duty.resume_duty_snapshot()) {
        return run_duty();
    }

    return {};
}

AutoTaskRunResult AutoTaskScheduler::dispatch_once() {
    auto& patrol = PatrolStateMachine::instance();
    auto& duty = DutyStateMachine::instance();

    auto patrol_task = patrol.query_patrol_task();
    auto duty_task = duty.query_duty_task();

    if (cancel_requested_.load()) return {};

    if (patrol_task && duty_task) {
        if (!simultaneous_reported_) {
            report_simultaneous_patrol_and_duty();
            simultaneous_reported_ = true;
        }
        AutoTaskRunResult result;
        result.conflict = true;
        return result;
    }
    simultaneous_reported_ = false;

    if (patrol_task) {
        patrol.init_patrol_task(std::move(*patrol_task));
        return run_patrol();
    }
    if (duty_task) {
        duty.init_duty_task(std::move(*duty_task));
        return run_duty();
    }

    return {};
}

void AutoTaskScheduler::prepare_dispatch() {
    cancel_requested_.store(false);
}

void AutoTaskScheduler::cancel() {
    cancel_requested_.store(true);
    switch (active_task_.load()) {
        case AutoTaskKind::PATROL:
            PatrolStateMachine::instance().request_cancel(true);
            return;
        case AutoTaskKind::DUTY:
            DutyStateMachine::instance().cancel();
            return;
        case AutoTaskKind::NONE:
            return;
    }
}

void AutoTaskScheduler::restart_last_task() {
    switch (last_task_.load()) {
        case AutoTaskKind::PATROL:
            PatrolStateMachine::instance().restart();
            return;
        case AutoTaskKind::DUTY:
            DutyStateMachine::instance().restart();
            return;
        case AutoTaskKind::NONE:
            SPDLOG_INFO("当前没有可重置的自动任务");
            return;
    }
}

void AutoTaskScheduler::forget_execution_marks() {
    SPDLOG_INFO("清除巡逻和值守的执行限制，下次调度允许强制执行");
    PatrolStateMachine::instance().set_force_patrol(true);
    DutyStateMachine::instance().set_force_duty(true);
}

AutoTaskKind AutoTaskScheduler::active_task() const {
    return active_task_.load();
}

std::string AutoTaskScheduler::get_task_uuid() {
    switch (active_task_.load()) {
        case AutoTaskKind::PATROL:
            return PatrolStateMachine::instance().get_patrol_uuid();
        case AutoTaskKind::DUTY:
            return DutyStateMachine::instance().get_duty_uuid();
        case AutoTaskKind::NONE:
            return "";
    }
    return "";
}

AutoTaskRunResult AutoTaskScheduler::run_patrol() {
    active_task_.store(AutoTaskKind::PATROL);
    last_task_.store(AutoTaskKind::PATROL);
    if (cancel_requested_.load()) {
        PatrolStateMachine::instance().request_cancel(true);
    }
    const auto outcome = PatrolStateMachine::instance().run_patrol_task();
    active_task_.store(AutoTaskKind::NONE);
    return {true, false, AutoTaskKind::PATROL, outcome};
}

AutoTaskRunResult AutoTaskScheduler::run_duty() {
    active_task_.store(AutoTaskKind::DUTY);
    last_task_.store(AutoTaskKind::DUTY);
    if (cancel_requested_.load()) {
        DutyStateMachine::instance().cancel();
    }
    const auto outcome = DutyStateMachine::instance().run_duty_task();
    active_task_.store(AutoTaskKind::NONE);
    return {true, false, AutoTaskKind::DUTY, outcome};
}

void AutoTaskScheduler::report_simultaneous_patrol_and_duty() {
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"},
            {"Sn", config_->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "计划冲突，同时出现巡逻值守"}}}
    };
    socket_to_web_.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
}
