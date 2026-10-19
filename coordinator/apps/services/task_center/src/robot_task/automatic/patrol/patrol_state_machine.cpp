#include "patrol_state_machine.hpp"
#include "patrol_states.hpp"
#include "patrol.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "component/action_executor/throughout_broadcaster.hpp"
#include "component/action_executor/door_opener/door_opener.hpp"

PatrolStateMachine& PatrolStateMachine::instance() {
    static PatrolStateMachine instance;
    return instance;
}

PatrolStateMachine::PatrolStateMachine() : current_state(PatrolState::IDLE), state_before_pause(PatrolState::IDLE), entry_state_(PatrolState::CHECK_CONDITION), patrol(std::make_unique<Patrol>()) {
    initialize_states();
    initialize_transitions();
}

PatrolStateMachine::~PatrolStateMachine() {

}

void PatrolStateMachine::initialize_states() {
    states_[PatrolState::IDLE] = std::make_shared<IdleState>();
    states_[PatrolState::QUERY_PLAN] = std::make_shared<QueryPlanState>();
    states_[PatrolState::CHECK_CONDITION] = std::make_shared<CheckBeforeDepartureState>();
    states_[PatrolState::PREPARE_PATROL] = std::make_shared<PreparePatrolState>();
    states_[PatrolState::START_CYCLE] = std::make_shared<StartCycleState>();
    states_[PatrolState::NAVIGATING] = std::make_shared<NavigatingState>();
    states_[PatrolState::EXECUTING_ACTION] = std::make_shared<ExecutingActionState>();
    states_[PatrolState::END_CYCLE] = std::make_shared<EndCycleState>();
    states_[PatrolState::WAITING_GAP] = std::make_shared<WaitingGapState>();
    states_[PatrolState::COMPLETED] = std::make_shared<CompletedState>();
    states_[PatrolState::CANCELLED] = std::make_shared<CancelledState>();
    states_[PatrolState::ERROR] = std::make_shared<ErrorState>();
}

void PatrolStateMachine::initialize_transitions() {
    transitions_[PatrolState::CHECK_CONDITION] = {
        {PatrolEvent::CHECK_PASSED, PatrolState::PREPARE_PATROL},
    };
    transitions_[PatrolState::PREPARE_PATROL] = {
        {PatrolEvent::PREPARATION_DONE, PatrolState::START_CYCLE}
    };
    transitions_[PatrolState::START_CYCLE] = {
        {PatrolEvent::CYCLE_STARTED, PatrolState::NAVIGATING}
    };
    transitions_[PatrolState::NAVIGATING] = {
        {PatrolEvent::NAVIGATION_SUCCESS, PatrolState::EXECUTING_ACTION},
        {PatrolEvent::END_TIME_REACHED, PatrolState::COMPLETED},
        {PatrolEvent::NAVIGATION_FAILED, PatrolState::ERROR},
        {PatrolEvent::NAVIGATION_TIMEOUT, PatrolState::ERROR},
        {PatrolEvent::CANCEL_REQUEST, PatrolState::CANCELLED},
    };
    transitions_[PatrolState::EXECUTING_ACTION] = {
        {PatrolEvent::CANCEL_REQUEST, PatrolState::CANCELLED},
    };
    transitions_[PatrolState::END_CYCLE] = {
        {PatrolEvent::CYCLE_ENDED, PatrolState::WAITING_GAP},
        {PatrolEvent::END_TIME_REACHED, PatrolState::COMPLETED}
    };
    transitions_[PatrolState::WAITING_GAP] = {
        {PatrolEvent::GAP_ELAPSED, PatrolState::START_CYCLE},
        {PatrolEvent::CANCEL_REQUEST, PatrolState::CANCELLED},
        {PatrolEvent::END_TIME_REACHED, PatrolState::COMPLETED}
    };
}

void PatrolStateMachine::transition(PatrolEvent event) {
    if (event == PatrolEvent::ERROR_OCCURRED) {
        change_state(PatrolState::ERROR);
        return;
    }

    if (event == PatrolEvent::NAVIGATION_SKIPPED || event == PatrolEvent::ACTION_COMPLETE || event == PatrolEvent::ACTION_FAILED) {
        if (event == PatrolEvent::NAVIGATION_SKIPPED) {
            auto point_name = context.get_current_point().point_name;
            SPDLOG_WARN("跳过不可达点位: {}", point_name);
        }

        context.move_to_next_point();

        if (context.has_more_points()) {
            SPDLOG_INFO("继续下一个点位: {}", context.current_point_index);
            change_state(PatrolState::NAVIGATING);
            return;
        }

        SPDLOG_INFO("第 {} 趟所有点位完成", context.cycle_count);
        change_state(PatrolState::END_CYCLE);
        return;
    }

    auto it = transitions_[current_state].find(event);
    if (it == transitions_[current_state].end()) {
        SPDLOG_WARN("未定义的状态转换: {} -> Event({})", states_[current_state]->get_state_name(), static_cast<int>(event));
        return;
    }
    change_state(it->second);
}

void PatrolStateMachine::change_state(PatrolState new_state) {
    if (new_state == current_state) return;
    states_[current_state]->on_exit(context);

    auto old_state = current_state;
    current_state = new_state;

    states_[current_state]->on_enter(context);
}

// ==================== 调度接口 ====================
std::optional<PatrolTask> PatrolStateMachine::query_patrol_task() {
    PatrolTask task = patrol->query_task();
    if (task.point_list.empty()) return std::nullopt;
    return task;
}

void PatrolStateMachine::init_patrol_task(PatrolTask task) {
    context.should_cancel.store(false);
    context.save_snapshot_on_cancel.store(false);
    context.error_message.clear();
    context.patrol_info = std::move(task);
    context.current_point_index = 0;
    context.cycle_count = 1;
    entry_state_ = PatrolState::CHECK_CONDITION;
}

TaskRunOutcome PatrolStateMachine::run_patrol_task() {
    running.store(true);
    change_state(entry_state_);
    TaskRunOutcome outcome = TaskRunOutcome::ERROR;
    while (true) {
        try {
            auto state = states_[current_state];
            PatrolEvent event = state->execute(context, *patrol);

            if (event == PatrolEvent::PAUSE_REQUEST) {
                event = PatrolEvent::CANCEL_REQUEST;
            }
            if (event == PatrolEvent::CANCEL_REQUEST) {
                handle_cancel_request();
            }
            if (event == PatrolEvent::CHECK_FAILED) {
                current_state = PatrolState::IDLE;
                outcome = TaskRunOutcome::NOT_STARTED;
                break;
            }
            if (is_terminal(current_state)) {
                if (current_state == PatrolState::COMPLETED) {
                    outcome = TaskRunOutcome::COMPLETED;
                } else if (current_state == PatrolState::CANCELLED) {
                    outcome = TaskRunOutcome::CANCELLED;
                } else {
                    outcome = TaskRunOutcome::ERROR;
                }
                current_state = PatrolState::IDLE;
                break;
            }
            transition(event);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("状态机异常: {}", e.what());
            context.error_message = e.what();
            transition(PatrolEvent::ERROR_OCCURRED);
        }
    }
    running.store(false);
    return outcome;
}

bool PatrolStateMachine::resume_patrol_snapshot() {
    if (!context_snapshot.has_value() || !is_snapshot()) {
        return false;
    }
    resume();
    entry_state_ = PatrolState::NAVIGATING;
    return true;
}

// ==================== 控制接口 ====================
void PatrolStateMachine::request_cancel(bool save_snapshot) {
    // 外部线程只提交取消请求，实际快照与清理由状态机线程处理。
    SPDLOG_INFO("提交巡逻取消请求, save_snapshot={}", save_snapshot);

    // 已收到的保存快照请求不能被后续普通取消覆盖。
    if (save_snapshot) {
        context.save_snapshot_on_cancel.store(true);
    }
    context.should_cancel.store(true);

    // Navigator 必须保证这两个方法可跨线程调用；sync_control 只写原子变量并 notify。
    Navigator::instance().sync_control(true, false);
    (void)Navigator::instance().cancel_navigation();
}

void PatrolStateMachine::handle_cancel_request() {
    const bool save_snapshot = context.save_snapshot_on_cancel.exchange(false);

    // 先保存长程动作参数，再停止动作，供后续恢复使用。
    if (save_snapshot) {
        snapshot();
    }

    auto& broadcaster = ThroughoutBroadcaster::getInstance();
    if (broadcaster.is_running()) {
        broadcaster.stop();
        SPDLOG_INFO("取消全程播报");
    }

    auto& door_opener = DoorOpener::getInstance();
    if (door_opener.is_running()) {
        door_opener.stop();
    }

    SPDLOG_INFO("巡逻取消请求处理完成, save_snapshot={}", save_snapshot);
}

void PatrolStateMachine::snapshot() {
    if (!running.load()) return;
    
    // 只有在活跃巡逻状态才保存快照
    bool is_active = current_state == PatrolState::PREPARE_PATROL || 
        current_state == PatrolState::NAVIGATING || 
        current_state == PatrolState::EXECUTING_ACTION || 
        current_state == PatrolState::WAITING_GAP;
    
    if (!is_active) return;

    // 是否有长程任务
    auto& broadcaster = ThroughoutBroadcaster::getInstance();
    const bool is_throughout_broadcaster = broadcaster.is_running();
    context_snapshot = PatrolSnapshot{
        context.patrol_info,
        context.current_point_index,
        context.cycle_count,
        context.patrol_report_uuid,
        is_throughout_broadcaster ? broadcaster.get_broadcast_text() : "",
        is_throughout_broadcaster ? broadcaster.get_broadcast_gap() : 0,
    };
    state_snapshot = current_state;

    std::string plan_name = context.patrol_info.plan_name;
    auto current_point = context.current_point_index + 1;
    auto total_point = context.patrol_info.point_list.size();
    auto cycle_count = context.cycle_count;
    SPDLOG_INFO("已保存巡逻快照: 计划={}, 点位 {}/{}, 第 {} 趟", plan_name, current_point, total_point, cycle_count);
}

void PatrolStateMachine::resume() {
    if (!context_snapshot.has_value()) {
        SPDLOG_WARN("没有保存的巡逻快照");
        return;
    }
    // 检查快照是否还有效
    if (!is_snapshot()) {
        SPDLOG_WARN("保存的快照已过期，清除");
        context_snapshot.reset();
        return;
    }
    SPDLOG_INFO("从快照恢复巡逻上下文");
    // 恢复上下文
    context.patrol_info = context_snapshot->patrol_info;
    context.current_point_index = context_snapshot->current_point_index;
    context.cycle_count = context_snapshot->cycle_count;
    context.patrol_report_uuid = context_snapshot->patrol_report_uuid;
    context.should_cancel.store(false);
    context.save_snapshot_on_cancel.store(false);
    context.error_message.clear();
    // 恢复全程语音播报
    if (!ThroughoutBroadcaster::getInstance().is_running() && !context_snapshot->throughout_broadcaster_text.empty()) {
        SPDLOG_INFO("恢复全程播报 text: {}, gap: {}", context_snapshot->throughout_broadcaster_text, context_snapshot->throughout_broadcaster_gap);
        ThroughoutBroadcaster::getInstance().start(context_snapshot->throughout_broadcaster_text, context_snapshot->throughout_broadcaster_gap);
    }
    context_snapshot.reset();
}

void PatrolStateMachine::restart() {
    SPDLOG_INFO("重置巡逻上下文, 重新开始");

    int plan_id = context.patrol_info.plan_id;
    if (plan_id <= 0 && context_snapshot.has_value()) {
        plan_id = context_snapshot->patrol_info.plan_id;
    }
    if (plan_id > 0) {
        patrol->clear_task_started(plan_id);
    }

    context.patrol_info = PatrolTask{};
    context.current_point_index = 0;
    context.cycle_count = 0;
    context.patrol_report_uuid = "";
    context.should_cancel.store(false);
    context.save_snapshot_on_cancel.store(false);
    context.error_message.clear();
    context_snapshot.reset();
    current_state = PatrolState::IDLE;
}

// ==================== 设置接口 ====================
void PatrolStateMachine::set_force_patrol(bool force) {
    force_patrol.store(force);
}

void PatrolStateMachine::set_resume_snapshot_pending() {
    SPDLOG_INFO("标记: 下次调度优先从快照恢复巡逻");
    resume_snapshot_pending_.store(true);
}

// ==================== 查询接口 ====================
PatrolState PatrolStateMachine::get_current_state() const {
    return current_state;
}

std::string PatrolStateMachine::get_patrol_uuid() {
    return context.patrol_report_uuid;
};

// ==================== 判断接口 ====================
bool PatrolStateMachine::is_running() const {
    return running.load();
}

bool PatrolStateMachine::is_terminal(PatrolState s) const {
    return s == PatrolState::COMPLETED || s == PatrolState::CANCELLED || s == PatrolState::ERROR;
}

bool PatrolStateMachine::is_cancel() const {
    return context.should_cancel.load();
}

bool PatrolStateMachine::is_snapshot() {
    if (!context_snapshot.has_value()) return false;

    // 检查是否过了结束时间
    if (context_snapshot->patrol_info.is_finish_soon && patrol->check_is_equal_or_after_end_time(context_snapshot->patrol_info)) {
        SPDLOG_INFO("快照对应的计划已过结束时间");
        return false;
    }

    // 可以加更多校验: 计划是否还存在、点位是否变化等
    return true;
}

bool PatrolStateMachine::is_force_patrol() const {
    return force_patrol.load();
}
