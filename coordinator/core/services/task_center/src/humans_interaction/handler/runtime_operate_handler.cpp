#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "mode/mode_service.hpp"
#include "robot_task/robot_task_coordinator.hpp"
#include "runtime_operate_handler.hpp"

RuntimeOperateHandler::RuntimeOperateHandler(zmq::socket_t& zmq_socket) : zmq_socket_(zmq_socket) {
    
}

RuntimeOperateHandler::~RuntimeOperateHandler() {

}

void RuntimeOperateHandler::handle_mode_change(const std::string& message) {
    try {
        // 应答
        nlohmann::json ack = {
            {"Header", {{"CmdType", "RC_RobotCtrlMode_Set"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

        // 校验
        nlohmann::json mode = nlohmann::json::parse(message).at("Mode");
        std::string unCtrlMode = mode.value("unCtrlMode", "");
        std::string unIsContinue = mode.value("unIsContinue", "");

        // 执行
        if (unCtrlMode == "0") {
            auto action_after_auto_mode = unIsContinue == "0" ? AutoModeOperation::RESTART : AutoModeOperation::CONTINUINGFROMLAST;
            ModeService::instance().switch_mode(RobotMode::AUTO, action_after_auto_mode);
        }
        if (unCtrlMode == "1") {
            ModeService::instance().switch_mode(RobotMode::MANUAL, std::nullopt);
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("切换模式, 发生异常: {}", e.what());
    }
}

void RuntimeOperateHandler::handle_pause_recovery(const std::string& message) {
    try {
        // 应答
        nlohmann::json ack = {
            {"Header", {{"CmdType", "RC_Robot_PatrolCtrl_Set"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

        // 校验
        if (ModeService::instance().is_manual_mode()) {
            SPDLOG_WARN("手动模式下, 不处理暂停巡逻 恢复巡逻命令");
            return;
        }

        // 执行
        auto type = nlohmann::json::parse(message).value("RobotPatrolCtrl", "");
        if (type == "0") {
            RobotTaskCoordinator::instance().cancel_auto_task();
            return;
        }
        if (type == "1") {
            RobotTaskCoordinator::instance().recover();
            return;
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("暂停巡逻, 发生异常: {}", e.what());
    }
}

void RuntimeOperateHandler::handle_to_charge(const std::string& message) {
    try {
        // 应答
        nlohmann::json ack = {
            {"Header", {{"CmdType", "RC_Robot_Ctrl"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

        // 校验
        auto robot_ctrl = nlohmann::json::parse(message).at("Robot_Ctrl");
        nlohmann::json type = robot_ctrl.at("unCtrlType");
        auto on_or_off = robot_ctrl.at("unCtrlValue");

        // 执行
        if (type != "2") {
            return;
        }
        if (on_or_off == "1") {
            RobotTaskCoordinator::instance().request_charging(ChargingReason::HUMAN_REQUEST);
            return;
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("人工操作充电, 发生异常: {}", e.what());
    }
}
