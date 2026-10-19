#include <exception>
#include <thread>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "broadcast_template.hpp"
#include "charging_executor.hpp"
#include "robot_task/charging/charging_point.hpp"
#include "component/navigate_executor/navigator.hpp"

ChargingExecutor::ChargingExecutor() {
    http = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
    docking_controller = std::make_unique<DockingController>(ctx);
}

ChargingExecutor::~ChargingExecutor() {
    cancel_charging();
}

void ChargingExecutor::prepare_run() {
    should_cancel.store(false);
    phase_.store(ChargingPhase::PREPARING);
}

ChargingOutcome ChargingExecutor::run(ChargingReason reason) {
    if (executing.exchange(true)) {
        SPDLOG_WARN("已在执行中");
        return ChargingOutcome::FAILED;
    }

    const auto execute = [this, reason]() -> ChargingOutcome {
        using namespace nlohmann;

        SPDLOG_INFO("开始充电流程：{}", charging_reason_name(reason));

        // 查询充电点
        if (!find_charge_point()) {
            return should_cancel.load() ? ChargingOutcome::CANCELLED : ChargingOutcome::FAILED;
        }

        // 准备前往充电点
        BroadcastTemplate tpl_start_charge{"charge.start.human_operate"};
        nlohmann::json params = {
            {"command", "broadcast-single-text"},
            {"content", tpl_start_charge.get_content("开始执行一键充电任务")}
        };
        socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);

        // 前往充电点
        phase_.store(ChargingPhase::NAVIGATING);
        const auto nav_result = nav_to_charge_point();
        phase_.store(ChargingPhase::PREPARING);
        if (!nav_result) {
            if (should_cancel.load()) return ChargingOutcome::CANCELLED;
            bool check_result = check_conditions_for_entering_hibernation();
            if (check_result) enter_hibernation();
            return ChargingOutcome::FAILED;
        }

        // 等待惯性稳定, 准备对接
        if (!interruptible_wait(std::chrono::seconds(1))) {
            return ChargingOutcome::CANCELLED;
        }
        BroadcastTemplate tpl_start_docking{"docking.start"};
        params["content"] = tpl_start_docking.get_content("到达充电点，开始对接");
        socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);

        // 对接
        phase_.store(ChargingPhase::DOCKING);
        auto docking_result = docking_controller->start([&]() { 
            auto navigate_result = Navigator::instance().nav_to_normal_point(current_map, point_name);
            return navigate_result == NavigationResult::SUCCESS;
        }, should_cancel);
        if (docking_result == DockingResult::SUCCESS) {
            phase_.store(ChargingPhase::DOCKED);
            BroadcastTemplate tpl_end_docking{"docking.end"};
            params["content"] = tpl_end_docking.get_content("对接成功，正在充电请谨慎操作");
            socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
            return ChargingOutcome::DOCKED;
        }

        if (docking_result == DockingResult::CANCELLED || should_cancel.load()) {
            return ChargingOutcome::CANCELLED;
        }
        bool check_result = check_conditions_for_entering_hibernation();
        if (check_result) enter_hibernation();
        return ChargingOutcome::FAILED;
    };

    ChargingOutcome outcome = ChargingOutcome::FAILED;
    try {
        outcome = execute();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("充电流程异常: {}", e.what());
    }

    if (outcome != ChargingOutcome::DOCKED) {
        phase_.store(ChargingPhase::IDLE);
    }
    executing.store(false);
    SPDLOG_INFO(
        "充电流程结束：{}, 结果: {}",
        charging_reason_name(reason),
        static_cast<int>(outcome));
    return outcome;
}

void ChargingExecutor::cancel_charging() {
    should_cancel.store(true);
    Navigator::instance().sync_control(true, false);
    (void)Navigator::instance().cancel_navigation();
}

void ChargingExecutor::finish_session() {
    phase_.store(ChargingPhase::IDLE);
}

bool ChargingExecutor::find_charge_point() {
    auto cp = charging::find_charging_point(*http);
    if (!cp) {
        BroadcastTemplate tpl_not_charge_point{"charge.start.not_charge_point"};
        nlohmann::json params = {
            {"command", "broadcast-single-text"},
            {"content", tpl_not_charge_point.get_content("当前地图没有充电点，请提前录制充电点")}
        };
        socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        return false;
    }
    if (should_cancel.load()) {
        SPDLOG_INFO("充电被取消（获取充电点前）");
        return false;
    }
    current_map = cp->map_name;
    point_name = cp->point_name;
    return true;
}

bool ChargingExecutor::nav_to_charge_point() {
    using namespace nlohmann;

    NavigationResult result = NavigationResult::FAILED;
    for (int retry = 0; retry < MAX_NAV_RETRY; ++retry) {
        if (should_cancel.load()) {
            SPDLOG_INFO("导航重试中, 充电被取消");
            return false;
        }
        
        if (retry > 0) {
            SPDLOG_INFO("导航重试 {}/{}", retry + 1, MAX_NAV_RETRY);
            if (!interruptible_wait(std::chrono::seconds(NAV_RETRY_DELAY_SECONDS))) {
                return false;
            }
        }
        
        result = Navigator::instance().nav_to_normal_point(current_map, point_name);
        if (result == NavigationResult::SUCCESS) {
            break;
        }
        SPDLOG_WARN("导航失败，结果: {}", static_cast<int>(result));
    }

    if (result != NavigationResult::SUCCESS) {
        SPDLOG_ERROR("充电点不可达，已重试 {} 次", MAX_NAV_RETRY);
        BroadcastTemplate tpl_can_not_to_point{"charge.start.can_not_to_point"};
        nlohmann::json params = {
            {"command", "broadcast-single-text"}, 
            {"content", tpl_can_not_to_point.get_content("充电点不可达，请检查路径是否被阻挡")}
        };
        socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        return false;
    }

    return true;
}

bool ChargingExecutor::interruptible_wait(std::chrono::milliseconds duration) const {
    constexpr auto slice = std::chrono::milliseconds(100);
    auto remaining = duration;
    while (remaining.count() > 0) {
        if (should_cancel.load()) return false;
        const auto current = remaining < slice ? remaining : slice;
        std::this_thread::sleep_for(current);
        remaining -= current;
    }
    return !should_cancel.load();
}

bool ChargingExecutor::is_active() const {
    return executing.load();
}

bool ChargingExecutor::is_going_to_charge() const {
    return phase_.load() == ChargingPhase::NAVIGATING;
}

bool ChargingExecutor::is_docking() const {
    return phase_.load() == ChargingPhase::DOCKING;
}

ChargingPhase ChargingExecutor::phase() const {
    return phase_.load();
}

// 去充电路上失败, 对桩失败 且 电量 <= 5%，且长时间无人介入，且是自动模式
bool ChargingExecutor::check_conditions_for_entering_hibernation() {
    return true;
}

// 进入冬眠状态
void ChargingExecutor::enter_hibernation() {

}
