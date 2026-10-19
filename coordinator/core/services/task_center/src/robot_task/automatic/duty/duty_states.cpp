#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "sole.hpp"
#include "duty_state_machine.hpp"
#include "duty.hpp"
#include "component/action_executor/action_executor.hpp"
#include "component/ptz_controller.hpp"

using namespace duty_task;

// ==================== 状态基类 ====================
void DutyStateBase::on_enter(DutyContext& ctx) {
    SPDLOG_INFO("进入状态: {}", get_state_name());
}
void DutyStateBase::on_exit(DutyContext& ctx) {
    SPDLOG_INFO("退出状态: {}", get_state_name());
}

// ==================== 空闲状态 ====================
DutyEvent IdleState::execute(DutyContext& ctx, Duty& duty) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return DutyEvent::START;
}
std::string IdleState::get_state_name() const {
    return "空闲";
}

// ==================== 查询计划状态 ====================
DutyEvent QueryPlanState::execute(DutyContext& ctx, Duty& duty) {
    // 清理状态，为下一轮做准备
    ctx.should_cancel.store(false);
    ctx.error_message.clear();

    DutyTask task = duty.query_task();
    if (task.plan_name.empty()) {
        return DutyEvent::NO_PLAN;
    }

    ctx.duty_info = std::move(task);
    return DutyEvent::PLAN_FOUND;
}
std::string QueryPlanState::get_state_name() const { 
    return "查询计划"; 
}

// =================== 值守前检查状态 ===================
DutyEvent CheckConditionState::execute(DutyContext& ctx, Duty& duty) {
    if (!duty.check_pre_duty(ctx)) {
        SPDLOG_WARN("检查不通过");
        return DutyEvent::CHECK_FAILED;
    }
    SPDLOG_INFO("值守前检查通过");
    return DutyEvent::CHECK_PASSED;
}
std::string CheckConditionState::get_state_name() const { 
    return "值守前检查";
}

// ==================== 准备值守状态 ====================
void PreparePatrolState::on_enter(DutyContext& ctx) {
    DutyStateBase::on_enter(ctx);
}
DutyEvent PreparePatrolState::execute(DutyContext& ctx, Duty& duty) {
    sole::uuid id = sole::uuid4();
    std::string uuid_string = id.str();
    ctx.duty_report_uuid = uuid_string;
    
    // 云台回中
    PTZController ptz_controller;
    ptz_controller.reset_ptz();

    SPDLOG_INFO("================= 开始执行值守计划 =================");
    duty.mark_task_started(ctx.duty_info.plan_id);
    // 上报开始值守
    duty.report_start_duty(ctx);
    return DutyEvent::PREPARATION_DONE;
}
std::string PreparePatrolState::get_state_name() const { 
    return "准备值守";
}

// ==================== 导航中状态 ====================
void NavigatingState::on_enter(DutyContext& ctx) {
    DutyStateBase::on_enter(ctx);
    const auto& point_name = ctx.duty_info.point_name;
    SPDLOG_INFO("前往点位: {}", point_name);
}
DutyEvent NavigatingState::execute(DutyContext& ctx, Duty& duty) {
    // 检查取消
    if (ctx.should_cancel.load()) {
        return DutyEvent::CANCEL_REQUEST;
    }
    // 检查结束时间
    if (duty.check_is_equal_or_after_end_time(ctx.duty_info)) {
        return DutyEvent::END_TIME_REACHED;
    }
    auto result = duty.navigate_to_point(ctx.duty_info.map_name, ctx.duty_info.point_name, ctx);
    ctx.last_nav_status.result = result;

    switch (result) {
        case NavigationResult::SUCCESS:
        case NavigationResult::SKIPPED: {
            SPDLOG_INFO("导航完成");
            return DutyEvent::NAVIGATION_SUCCESS;
        }
        case NavigationResult::PAUSED: {
            SPDLOG_INFO("导航被暂停");
            return DutyEvent::PAUSE_REQUEST;
        }
        case NavigationResult::LOCALIZATION_LOST: {
            ctx.error_message = "定位持续丢失";
            SPDLOG_ERROR("定位丢失,进入错误状态");
            return DutyEvent::NAVIGATION_FAILED;
        }
        case NavigationResult::TIMEOUT: {
            ctx.error_message = "导航超时";
            return DutyEvent::NAVIGATION_TIMEOUT;
        }
        case NavigationResult::CANCELLED: {
            return DutyEvent::CANCEL_REQUEST;
        }
        case NavigationResult::FAILED:
        default: {
            ctx.error_message = "导航失败";
            return DutyEvent::NAVIGATION_FAILED;
        }
    }
}
std::string NavigatingState::get_state_name() const { 
    return "导航中";
}

// ==================== 执行动作状态 ====================
DutyEvent ExecutingActionState::execute(DutyContext& ctx, Duty& duty) {
    std::string point_name = ctx.duty_info.point_name;
    const auto& action_group_id = ctx.duty_info.action_group_id;
    
    if (action_group_id == 0) {
        SPDLOG_INFO("无动作组,跳过");
        return DutyEvent::ACTION_COMPLETE;
    }
    SPDLOG_INFO("执行动作组: {}", action_group_id);

    try {
        std::atomic<bool> should_pause_{false};
        ActionExecutor::getInstance().execute_action_list(
            action_group_id, 
            point_name, 
            ctx.duty_report_uuid,
            ctx.should_cancel, 
            should_pause_
        );
        return DutyEvent::ACTION_COMPLETE;
    } catch (const std::exception& e) {
        ctx.error_message = std::string("动作执行失败: ") + e.what();
        return DutyEvent::ACTION_FAILED;
    }
}
std::string ExecutingActionState::get_state_name() const { 
    return "执行动作组";
}

// ==================== 等待间隔状态 ====================
DutyEvent WaitingGapState::execute(DutyContext& ctx, Duty& duty) {
    // 先检查是否已到达结束时间
    if (duty.check_is_equal_or_after_end_time(ctx.duty_info)) {
        SPDLOG_INFO("已到达结束时间，停止巡逻");
        return DutyEvent::END_TIME_REACHED;
    }
    // 如果没有间隔时间，直接开始下一动作组
    if (ctx.duty_info.gap <= 0) {
        return DutyEvent::GAP_ELAPSED;
    }
    // 可中断的等待
    for (int i = 0; i < ctx.duty_info.gap * 60; ++i) {
        if (ctx.should_cancel.load()) {
            return DutyEvent::CANCEL_REQUEST;
        }
        // 等待期间也要检查结束时间
        if (duty.check_is_equal_or_after_end_time(ctx.duty_info)) {
            SPDLOG_INFO("等待期间到达结束时间，停止值守");
            return DutyEvent::END_TIME_REACHED;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    // 等待结束后再次检查结束时间
    if (duty.check_is_equal_or_after_end_time(ctx.duty_info)) {
        SPDLOG_INFO("间隔等待结束，已到达结束时间，停止值守");
        return DutyEvent::END_TIME_REACHED;
    }
    return DutyEvent::GAP_ELAPSED;
}
std::string WaitingGapState::get_state_name() const { return "等待间隔"; }

// ==================== 完成状态 =======================
void CompletedState::on_enter(DutyContext& ctx) {
    DutyStateBase::on_enter(ctx);
}
DutyEvent CompletedState::execute(DutyContext& ctx, Duty& duty) {
    SPDLOG_INFO("================= 巡逻计划执行结束 =================");
    duty.report_end_duty(ctx);
    return DutyEvent::START;
}
std::string CompletedState::get_state_name() const { return "完成"; }

// ===================== 取消状态 ======================
void CancelledState::on_enter(DutyContext& ctx) {
    DutyStateBase::on_enter(ctx);
}
DutyEvent CancelledState::execute(DutyContext& ctx, Duty& duty) {
    SPDLOG_INFO("================= 巡逻计划被取消 =================");

    // 清理状态
    ctx.should_cancel.store(false);
    ctx.error_message.clear();

    return DutyEvent::START;
}
std::string CancelledState::get_state_name() const { return "已取消"; }

// ==================== 错误状态 ======================
DutyEvent ErrorState::execute(DutyContext& ctx, Duty& duty) {
    SPDLOG_ERROR("进入错误状态: {}", ctx.error_message);
    // 可以实现重试逻辑
    std::this_thread::sleep_for(std::chrono::seconds(10));    
    // 暂时简单处理,返回空闲
    return DutyEvent::START;
}
std::string ErrorState::get_state_name() const { return "错误"; }
