#include <spdlog/spdlog.h>
#include "magic_enum/magic_enum.hpp"
#include "mode/mode_service.hpp"
#include "robot_task/robot_task_coordinator.hpp"
#include "after_plan_save_handler.hpp"

AfterUpdatePlanHandler::AfterUpdatePlanHandler() {

}

AfterUpdatePlanHandler::~AfterUpdatePlanHandler() {

}

void AfterUpdatePlanHandler::handle_after_plan_save() {
    try {
        if (!RobotTaskCoordinator::instance().handle_plan_updated()) {
            return;
        }
        if (ModeService::instance().is_manual_mode()) {
            ModeService::instance().switch_mode(RobotMode::AUTO, AutoModeOperation::RESTART);
            SPDLOG_INFO("切换到自动模式");
        } else {
            SPDLOG_INFO("计划已更新, 请求调度器重新调度");
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("保存计划, 发生异常: {}", e.what());
    }
}
