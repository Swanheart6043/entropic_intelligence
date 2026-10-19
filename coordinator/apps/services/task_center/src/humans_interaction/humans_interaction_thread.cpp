#include <iostream>
#include <functional>
#include <unordered_map>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "./handler/runtime_operate_handler.hpp"
#include "./handler/navigation_handler.hpp"
#include "./handler/initialization_handler.hpp"
#include "./handler/ptz_operate_handler.hpp"
#include "./handler/equipment_handler.hpp"
#include "robot_task/manual/manual_motion_executor.hpp"
#include "humans_interaction_thread.hpp"

UserInteractionThread::UserInteractionThread() {
    int hwm = 50;
    int timeout = 300; 
    socket.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket.bind("tcp://*:4004");
    socket_to_web_gateway.connect("tcp://127.0.0.1:2007");
}

UserInteractionThread::~UserInteractionThread() {
    stop();
}

void UserInteractionThread::start() {
    if (running) {
        SPDLOG_WARN("用户交互线程已经在启动了");
        return;
    }
    ManualMotionExecutor::instance().start(); // 先启动执行器，监听线程收到命令后可立即提交
    running = true;
    worker_thread = std::thread(&UserInteractionThread::message_loop, this);
    SPDLOG_INFO("用户交互线程启动");
}

void UserInteractionThread::stop() {
    if (!running) {
        SPDLOG_WARN("用户交互线程没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
    ManualMotionExecutor::instance().stop(); // 收命令线程退出后再停止执行器，确保不会出现新的提交
    SPDLOG_INFO("用户交互线程停止");
}

void UserInteractionThread::message_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto recv_result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!recv_result.has_value()) continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());

            match_command(message);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("控制线程异常，线程退出: {}", e.what());
        }
    }
}

void UserInteractionThread::match_command(const std::string& message) {
    using namespace nlohmann;

    std::unordered_map<std::string, std::function<void(const std::string&)>> command_handlers = {
        // 初始化 - 自定义转圈
        {"RC_Robot_Initialize_Customized", [&](const std::string& message) { 
            nlohmann::json ack = {
                {"Header", {
                    {"CmdType", "RC_Robot_Initialize_Customized"}, 
                    {"Sn", config->get_sn()}, 
                    {"Time", std::time(nullptr)}
                }},
                {"Result", 1}, {"Data", ""}
            };
            socket_to_web_gateway.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
            InitializationController initialization_control;
            initialization_control.initialize_wait(message, socket_to_web_gateway);
        }},
        // 初始化 - 自定义不转圈
        {"RC_Robot_Initialize_Customized_Directly", [&](const std::string& message) { 
            nlohmann::json ack = {
                {"Header", {
                    {"CmdType", "RC_Robot_Initialize_Customized_Directly"}, 
                    {"Sn", config->get_sn()}, 
                    {"Time", std::time(nullptr)}
                }},
                {"Result", 1}, {"Data", ""}
            };
            socket_to_web_gateway.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
            InitializationController initialization_control;
            initialization_control.initialize_un_wait(message, socket_to_web_gateway);
        }},
        // 初始化 - 查看有无结束
        {"RC_Robot_Is_Initialize_Finished", [&](const std::string& message) { 
            nlohmann::json ack = {
                {"Header", {
                    {"CmdType", "RC_Robot_Is_Initialize_Finished"}, 
                    {"Sn", config->get_sn()}, 
                    {"Time", std::time(nullptr)}
                }},
                {"Result", 1}, {"Data", ""}
            };
            socket_to_web_gateway.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
            InitializationController initialization_control;
            initialization_control.initialize_check_finish(socket_to_web_gateway);
        }},
        // 初始化 - 停止
        {"RC_Robot_Stop_Initialize", [&](const std::string& message) { 
            nlohmann::json ack = {
                {"Header", {
                    {"CmdType", "RC_Robot_Stop_Initialize"}, 
                    {"Sn", config->get_sn()}, 
                    {"Time", std::time(nullptr)}
                }},
                {"Result", 1}, {"Data", ""}
            };
            socket_to_web_gateway.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
            InitializationController initialization_control;
            initialization_control.initialize_stop(socket_to_web_gateway);
        }},
        // 模式 - 自动手动
        {"RC_RobotCtrlMode_Set", [&](const std::string& message) { 
            RuntimeOperateHandler runtime_operate_handler(socket_to_web_gateway);
            runtime_operate_handler.handle_mode_change(message);
        }},
        // 暂停巡逻, 恢复巡逻
        {"RC_Robot_PatrolCtrl_Set", [&](const std::string& message) {
            RuntimeOperateHandler runtime_operate_handler(socket_to_web_gateway);
            runtime_operate_handler.handle_pause_recovery(message);
        }},
        // 导航 - 充电
        {"RC_Robot_Ctrl", [&](const std::string& message) {
            RuntimeOperateHandler runtime_operate_handler(socket_to_web_gateway);
            runtime_operate_handler.handle_to_charge(message);
        }},
        // 导航 - 移动
        {"RC_RobotRun_Ctrl", [&](const std::string& message) {
            NavigationHandler navigation_handler(socket_to_web_gateway);
            navigation_handler.move(message);
        }},
        // 导航 - 到导航点，导航到任意点
        {"RC_Robot_Navigate_To_Point", [&](const std::string& message) {
            NavigationHandler navigation_handler(socket_to_web_gateway);
            navigation_handler.go_to_point(message);
        }},
        // 导航 - 暂停、恢复、取消导航到指定点
        {"RC_Robot_Navigate_Execution_Control", [&](const std::string& message) {
            NavigationHandler navigation_handler(socket_to_web_gateway);
            navigation_handler.control_to_point(message);
        }},
        // 导航 - 跑线
        {"NB_RobotRunOnce_Laser_Set", [&](const std::string& message) {
            NavigationHandler navigation_handler(socket_to_web_gateway);
            navigation_handler.run_line(message);
        }},
        // 预置位 - 新增编辑
        {"RC_Robot_Preset_Set", [&](const std::string& message) {
            PtzOperateHandler ptz_operate_handler(socket_to_web_gateway);
            ptz_operate_handler.match_preset_position_operate(message);
        }},
        // 转动云台
        {"RC_Robot_PTZ_Ctrl", [&](const std::string& message) {
            PtzOperateHandler ptz_operate_handler(socket_to_web_gateway);
            ptz_operate_handler.match_ptz_operate(message);
        }},
        // 解除急停
        {"RC_Robot_Release_Scram", [&](const std::string& message) {
            EquipmentHandler equipment_handler(socket_to_web_gateway);
            equipment_handler.release_emergency_stop();
        }},
        // 控制雨刷 音量 大灯 警示灯
        {"RC_RobotEnable_Set", [&](const std::string& message) {
            EquipmentHandler equipment_handler(socket_to_web_gateway);
            equipment_handler.handle(message);
        }},
        // 重启, 关机
        {"RC_Robot_Restart_Ctrl", [&](const std::string& message) { 
            EquipmentHandler equipment_handler(socket_to_web_gateway);
            equipment_handler.match_reboot_operate(message);
        }},
    };
    
    try {
        auto task_name = json::parse(message).at("Header").at("CmdType");
        auto task = command_handlers.find(task_name);
        if (task == command_handlers.end()) {
            SPDLOG_ERROR("未知任务: {}", task_name.dump());
            return;
        }
        SPDLOG_INFO("监听到指令: {}", message);
        task->second(message);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("匹配指令异常: {}", e.what());
    }
}
