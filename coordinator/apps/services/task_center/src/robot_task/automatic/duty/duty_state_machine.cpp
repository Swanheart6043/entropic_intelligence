#include "duty_state_machine.hpp"
#include "duty.hpp"
#include "component/navigate_executor/navigator.hpp"

using namespace duty_task;

DutyStateMachine& DutyStateMachine::instance() {
    static DutyStateMachine instance;
    return instance;
}

DutyStateMachine::DutyStateMachine() : current_state(DutyState::IDLE), duty(std::make_unique<Duty>()) {
    initialize_states();
    initialize_transitions();
}

DutyStateMachine::~DutyStateMachine() = default;

void DutyStateMachine::initialize_states() {
    states[DutyState::IDLE] = std::make_shared<IdleState>();
    states[DutyState::QUERY_PLAN] = std::make_shared<QueryPlanState>();
    states[DutyState::CHECK_CONDITION] = std::make_shared<CheckConditionState>();
    states[DutyState::PREPARE_PATROL] = std::make_shared<PreparePatrolState>();
    states[DutyState::NAVIGATING] = std::make_shared<NavigatingState>();
    states[DutyState::EXECUTING_ACTION] = std::make_shared<ExecutingActionState>();
    states[DutyState::WAITING_GAP] = std::make_shared<WaitingGapState>();
    states[DutyState::ERROR] = std::make_shared<ErrorState>();
    states[DutyState::COMPLETED] = std::make_shared<CompletedState>();
    states[DutyState::CANCELLED] = std::make_shared<CancelledState>();
}

void DutyStateMachine::initialize_transitions() {
    transitions[DutyState::CHECK_CONDITION] = {
        {DutyEvent::CHECK_PASSED, DutyState::PREPARE_PATROL},
    };
    transitions[DutyState::PREPARE_PATROL] = {
        {DutyEvent::PREPARATION_DONE, DutyState::NAVIGATING}
    };
    transitions[DutyState::NAVIGATING] = {
        {DutyEvent::NAVIGATION_SUCCESS, DutyState::EXECUTING_ACTION},
        {DutyEvent::NAVIGATION_FAILED, DutyState::ERROR},
        {DutyEvent::NAVIGATION_TIMEOUT, DutyState::ERROR},
        {DutyEvent::CANCEL_REQUEST, DutyState::CANCELLED},
        {DutyEvent::END_TIME_REACHED, DutyState::COMPLETED}
    };
    transitions[DutyState::WAITING_GAP] = {
        {DutyEvent::GAP_ELAPSED, DutyState::EXECUTING_ACTION},
        {DutyEvent::CANCEL_REQUEST, DutyState::CANCELLED},
        {DutyEvent::END_TIME_REACHED, DutyState::COMPLETED}
    };
}

void DutyStateMachine::transition(DutyEvent event) {
    if (event == DutyEvent::ERROR_OCCURRED) {
        change_state(DutyState::ERROR);
        return;
    }
    if (event == DutyEvent::ACTION_COMPLETE || event == DutyEvent::ACTION_FAILED) {
        change_state(DutyState::WAITING_GAP);
        return;
    }
    auto it = transitions[current_state].find(event);
    if (it == transitions[current_state].end()) {
        SPDLOG_WARN("未定义的状态转换: {} -> Event({})", states[current_state]->get_state_name(), static_cast<int>(event));
        return;
    }
    change_state(it->second);
}

void DutyStateMachine::change_state(DutyState new_state) {
    if (new_state == current_state) return;
    states[current_state]->on_exit(context);
    auto old_state = current_state;
    current_state = new_state;
    states[current_state]->on_enter(context);
}

// ==================== 调度接口 ====================
std::optional<DutyTask> DutyStateMachine::query_duty_task() {
    DutyTask task = duty->query_task();
    if (task.plan_name.empty()) return std::nullopt;
    return task;
}

void DutyStateMachine::init_duty_task(DutyTask task) {
    context.should_cancel.store(false);
    context.error_message.clear();
    context.duty_info = std::move(task);
    entry_state = DutyState::CHECK_CONDITION;
}

TaskRunOutcome DutyStateMachine::run_duty_task() {
    running.store(true);
    change_state(DutyState::CHECK_CONDITION);

    while (true) {
        try {
            auto state = states[current_state];
            DutyEvent event = state->execute(context, *duty);

            // ---- 检查未通过: 任务未开始, 交还调度器(调度器会退避后重查) ----
            if (event == DutyEvent::CHECK_FAILED) {
                current_state = DutyState::IDLE;
                running.store(false);
                return TaskRunOutcome::NOT_STARTED;
            }

            // ---- 终态: 其 execute 已完成收尾, 返回结果 ----
            if (is_terminal(current_state)) {
                TaskRunOutcome outcome = TaskRunOutcome::ERROR;
                if (current_state == DutyState::COMPLETED) outcome = TaskRunOutcome::COMPLETED;
                if (current_state == DutyState::CANCELLED) outcome = TaskRunOutcome::CANCELLED;
                current_state = DutyState::IDLE;
                running.store(false);
                return outcome;
            }

            transition(event);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("状态机异常: {}", e.what());
            context.error_message = e.what();
            transition(DutyEvent::ERROR_OCCURRED);
        }
    }
}

bool DutyStateMachine::resume_duty_snapshot() {
    return false;
}

// ==================== 控制接口 ====================
void DutyStateMachine::cancel() {
    SPDLOG_INFO("取消值守, 当前状态: {}", states[current_state]->get_state_name());
    context.should_cancel.store(true);
    Navigator::instance().cancel_navigation();
}

void DutyStateMachine::restart() {
    SPDLOG_INFO("重置值守上下文, 重新开始");

    if (context.duty_info.plan_id > 0) {
        duty->clear_task_started(context.duty_info.plan_id);
    }

    context.duty_info = DutyTask{};
    context.duty_report_uuid = "";
    context.should_cancel.store(false);
    context.error_message.clear();

    // 直接复位, 不走 change_state
    current_state = DutyState::IDLE;
}

// ==================== 设置接口 ====================
void DutyStateMachine::set_force_duty(bool force) {
    force_duty.store(force);
}

// ==================== 查询接口 ====================
std::string DutyStateMachine::get_duty_uuid() {
    return context.duty_report_uuid;
}

// ==================== 判断接口 ====================
bool DutyStateMachine::is_running() const {
    return running.load();
}

bool DutyStateMachine::is_force_duty() const {
    return force_duty.load();
}

bool DutyStateMachine::is_terminal(DutyState s) const {
    return s == DutyState::COMPLETED || s == DutyState::CANCELLED || s == DutyState::ERROR;
}
