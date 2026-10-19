#include <nlohmann/json.hpp>
#include "patrol_state_machine.hpp"
#include "patrol_states.hpp"
#include "patrol.hpp"
#include "component/action_executor/action_executor.hpp"
#include "component/action_executor/throughout_broadcaster.hpp"

void PatrolStateBase::on_enter(PatrolContext& ctx) {
    SPDLOG_INFO("进入状态: {}", get_state_name());
}

void PatrolStateBase::on_exit(PatrolContext& ctx) {
    SPDLOG_INFO("退出状态: {}", get_state_name());
}

// ================================ 空闲状态 ====================================
void IdleState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent IdleState::execute(PatrolContext& ctx, Patrol& patroller) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    return PatrolEvent::START;
}
std::string IdleState::get_state_name() const { return "空闲"; }

// ================================ 查询计划状态 ================================
void QueryPlanState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent QueryPlanState::execute(PatrolContext& ctx, Patrol& patroller) {
    // 清理状态，为下一轮做准备
    ctx.should_cancel.store(false);
    ctx.error_message.clear();

    PatrolTask task = patroller.query_task();
    if (task.point_list.empty()) {
        return PatrolEvent::NO_PLAN;
    }

    SPDLOG_INFO("查询到巡逻计划: {}", task.plan_name);

    // 恢复状态 - 之前被取消
    ctx.patrol_info = std::move(task);
    ctx.current_point_index = 0;
    ctx.cycle_count = 1;
    return PatrolEvent::PLAN_FOUND;
}
std::string QueryPlanState::get_state_name() const { return "查询计划"; }

// =============================== 巡逻前检查状态 ===============================
void CheckBeforeDepartureState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent CheckBeforeDepartureState::execute(PatrolContext& ctx, Patrol& patroller) {
    bool passed = patroller.check_pre_patrol(ctx);
    PatrolStateMachine::instance().set_force_patrol(false);
    
    if (!passed) {
        SPDLOG_WARN("检查不通过");
        return PatrolEvent::CHECK_FAILED;
    }
    SPDLOG_INFO("检查通过");
    return PatrolEvent::CHECK_PASSED;
}
std::string CheckBeforeDepartureState::get_state_name() const { return "巡逻前检查"; }

// ================================ 准备巡逻状态 ================================
void PreparePatrolState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent PreparePatrolState::execute(PatrolContext& ctx, Patrol& patroller) {
    patroller.init_patrol(ctx);
    patroller.mark_task_started(ctx.patrol_info.plan_id);
    return PatrolEvent::PREPARATION_DONE;
}
std::string PreparePatrolState::get_state_name() const { return "准备巡逻"; }

// ================================ 开始新趟状态 ================================
void StartCycleState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent StartCycleState::execute(PatrolContext& ctx, Patrol& patroller) {
    SPDLOG_INFO("========== 开始第 {} 趟巡逻 ==========", ctx.cycle_count);
    patroller.start_new_cycle(ctx);
    return PatrolEvent::CYCLE_STARTED;
}
std::string StartCycleState::get_state_name() const { return "开始新趟"; }

// ================================ 导航中状态 ==================================
void NavigatingState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
    const auto& point = ctx.get_current_point();
    SPDLOG_INFO("前往点位: {}/{}, 点位名: {}", ctx.current_point_index + 1, ctx.patrol_info.point_list.size(), point.point_name);
}
PatrolEvent NavigatingState::execute(PatrolContext& ctx, Patrol& patroller) {
    // 检查取消
    if (ctx.should_cancel.load()) {
        return PatrolEvent::CANCEL_REQUEST;
    }
    // 检查结束时间
    if (ctx.patrol_info.is_finish_soon && patroller.check_is_equal_or_after_end_time(ctx.patrol_info)) {
        return PatrolEvent::END_TIME_REACHED;
    }
    
    const auto& point = ctx.get_current_point();
    auto result = patroller.navigate_to_point(point, ctx);
    ctx.last_nav_status.result = result;
    
    // 检查取消
    if (ctx.should_cancel.load()) {
        return PatrolEvent::CANCEL_REQUEST;
    }

    switch (result) {
        case NavigationResult::SUCCESS: {
            patroller.handle_arrival_point(ctx);
            return PatrolEvent::NAVIGATION_SUCCESS;
        }
        case NavigationResult::SKIPPED: {
            return PatrolEvent::NAVIGATION_SKIPPED;
        }
        case NavigationResult::PAUSED: {
            return PatrolEvent::PAUSE_REQUEST;
        }
        case NavigationResult::CANCELLED: {
            return PatrolEvent::CANCEL_REQUEST;
        }
        case NavigationResult::LOCALIZATION_LOST: {
            ctx.error_message = "定位持续丢失";
            return PatrolEvent::NAVIGATION_FAILED;
        }
        case NavigationResult::TIMEOUT: {
            ctx.error_message = "导航超时";
            return PatrolEvent::NAVIGATION_TIMEOUT;
        }
        case NavigationResult::FAILED: {
            ctx.error_message = "导航失败";
            return PatrolEvent::NAVIGATION_FAILED;
        }
        default: {
            ctx.error_message = "未知异常";
            return PatrolEvent::NAVIGATION_FAILED;
        }
    }
}
std::string NavigatingState::get_state_name() const { return "导航中"; }

// ================================ 执行动作状态 ================================
void ExecutingActionState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent ExecutingActionState::execute(PatrolContext& ctx, Patrol& patroller) {
    // 检查取消
    if (ctx.should_cancel.load()) {
        return PatrolEvent::CANCEL_REQUEST;
    }
    
    const auto& point = ctx.get_current_point();
    if (point.action_group_id == 0) {
        SPDLOG_INFO("无动作组, 跳过");
        return PatrolEvent::ACTION_COMPLETE;
    }
    SPDLOG_INFO("执行动作组: {}", point.action_group_id);

    try {
        std::atomic<bool> dummy_pause_flag{false};
        ActionExecutor::getInstance().execute_action_list(
            point.action_group_id, 
            point.point_name, 
            ctx.patrol_report_uuid,
            ctx.should_cancel,
            dummy_pause_flag
        );
        // 再次检查状态
        if (ctx.should_cancel.load()) {
            return PatrolEvent::CANCEL_REQUEST;
        }
        return PatrolEvent::ACTION_COMPLETE;
    } catch (const std::exception& e) {
        ctx.error_message = std::string("动作执行失败: ") + e.what();
        return PatrolEvent::ACTION_FAILED;
    }
}
std::string ExecutingActionState::get_state_name() const { return "执行动作组"; }

// ================================ 结束当前趟状态 ==============================
void EndCycleState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent EndCycleState::execute(PatrolContext& ctx, Patrol& patroller) {
    SPDLOG_INFO("========== 完成第 {} 趟巡逻 ==========", ctx.cycle_count);
    
    // 上报巡逻结束
    patroller.reporting_data_end_navigation(ctx);
    
    // 检查执行模式
    if (ctx.patrol_info.execution_mode == plan::ExecutionMode::ONCE) {
        SPDLOG_INFO("单次执行模式，巡逻结束");
        return PatrolEvent::END_TIME_REACHED;  // 单次模式结束
    }
    
    // 循环模式：准备下一趟
    ctx.reset_for_next_cycle();
    SPDLOG_INFO("准备进入下一趟巡逻");
    
    return PatrolEvent::CYCLE_ENDED;
}
std::string EndCycleState::get_state_name() const { return "结束当前趟"; }

// ================================ 等待间隔状态 ================================
void WaitingGapState::on_enter(PatrolContext& ctx) {
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent WaitingGapState::execute(PatrolContext& ctx, Patrol& patroller) {
    // 先检查是否已到达结束时间
    if (patroller.check_is_equal_or_after_end_time(ctx.patrol_info)) {
        SPDLOG_INFO("已到达结束时间，停止巡逻");
        return PatrolEvent::END_TIME_REACHED;
    }
    // 如果没有间隔时间，直接开始下一趟
    if (ctx.patrol_info.gap <= 0) {
        return PatrolEvent::GAP_ELAPSED;
    }
    SPDLOG_INFO("等待 {} 分钟后继续下一趟巡逻", ctx.patrol_info.gap);
    
    // 可中断的等待
    for (int i = 0; i < ctx.patrol_info.gap * 60; ++i) {
        if (ctx.should_cancel.load()) {
            return PatrolEvent::CANCEL_REQUEST;
        }
        // 等待期间也要检查结束时间
        if (patroller.check_is_equal_or_after_end_time(ctx.patrol_info)) {
            SPDLOG_INFO("等待期间到达结束时间，停止巡逻");
            return PatrolEvent::END_TIME_REACHED;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    // 等待结束后再次检查结束时间
    if (patroller.check_is_equal_or_after_end_time(ctx.patrol_info)) {
        SPDLOG_INFO("间隔等待结束，但已到达结束时间，停止巡逻");
        return PatrolEvent::END_TIME_REACHED;
    }

    // ========== 间隔结束,准备开始新一趟,上报开始 ==========
    // patroller.reporting_data_start_navigation(ctx);
    return PatrolEvent::GAP_ELAPSED;
}
std::string WaitingGapState::get_state_name() const { return "等待间隔"; }

// ================================ 完成状态 ===================================
void CompletedState::on_enter(PatrolContext& ctx) {
    SPDLOG_INFO("================= 巡逻计划执行完成 =================");
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent CompletedState::execute(PatrolContext& ctx, Patrol& patroller) {
    ThroughoutBroadcaster::getInstance().stop();
    return PatrolEvent::START;
}
std::string CompletedState::get_state_name() const { return "巡逻完成"; }

// ================================= 取消状态 ==================================
void CancelledState::on_enter(PatrolContext& ctx) {
    SPDLOG_INFO("================= 巡逻计划被取消 =================");
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent CancelledState::execute(PatrolContext& ctx, Patrol& patroller) {
    patroller.reporting_data_end_navigation(ctx);
    // 清理状态
    ctx.should_cancel.store(false);
    ctx.error_message.clear();
    return PatrolEvent::START;
}
std::string CancelledState::get_state_name() const { return "已取消"; }

// ================================ 错误状态 ===================================
void ErrorState::on_enter(PatrolContext& ctx) {
    SPDLOG_INFO("================= 巡逻计划遇错误 =================");
    PatrolStateBase::on_enter(ctx);
}
PatrolEvent ErrorState::execute(PatrolContext& ctx, Patrol& patroller) {
    patroller.reporting_data_end_navigation(ctx);
    SPDLOG_ERROR("进入错误状态: {}", ctx.error_message);
    return PatrolEvent::START;
}
std::string ErrorState::get_state_name() const { return "遇错误"; }