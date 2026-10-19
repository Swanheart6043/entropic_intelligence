#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "internal_interaction_thread.hpp"
#include "robot_task/robot_task_coordinator.hpp"
#include "./handler/after_plan_save_handler.hpp"
#include "./handler/battery_too_hot_handler.hpp"

InternalCommandThread::InternalCommandThread() {
    int timeout = 300; 
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket.bind("tcp://*:8223");
    socket_rep.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket_rep.bind("ipc://@task_center_req_rep");
}

InternalCommandThread::~InternalCommandThread() {
    stop();
}

void InternalCommandThread::start() {
    if (running) {
        SPDLOG_WARN("内部命令线程已经在启动了");
        return;
    }
    running = true;
    pull_thread = std::thread(&InternalCommandThread::pull_loop, this);
    rep_thread = std::thread(&InternalCommandThread::rep_loop, this);
    SPDLOG_INFO("内部命令线程启动");
}

void InternalCommandThread::stop() {
    if (!running) {
        SPDLOG_WARN("内部命令线程没有启动，无需停止");
        return;
    }
    running = false;
    if (pull_thread.joinable()) pull_thread.join();
    if (rep_thread.joinable()) rep_thread.join();
    SPDLOG_INFO("内部命令线程停止");
}

void InternalCommandThread::pull_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!result.has_value()) continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("收到内部命令: {}", message);
            match_message(message);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("内部命令异常: {}", e.what());
        }
    }
}

void InternalCommandThread::match_message(const std::string& message) {
    auto message_json = nlohmann::json::parse(message);
    // 更新计划后
    if (message_json.at("command").get<std::string>() == "plan_has_been_updated") {
        AfterUpdatePlanHandler after_update_plan_handler;
        after_update_plan_handler.handle_after_plan_save();
        return;
    }
    // 低电量充电
    if (message_json.at("command").get<std::string>() == "low_power_go_to_charge") {
        RobotTaskCoordinator::instance().request_charging(ChargingReason::LOW_POWER);
        return;
    }
    // 充电完成
    if (message_json.at("command").get<std::string>() == "charging-complete") {
        RobotTaskCoordinator::instance().notify_charging_complete();
        return;
    }
    // 告警联动
    if (message_json.at("command").get<std::string>() == "alarm-actions") {
        alarm_action_handler.handle_async(message);
        return;
    }
    // SOS按下
    if (message_json.at("command").get<std::string>() == "sos-pressed") {
        RobotTaskCoordinator::instance().cancel();
        return;
    }
    // SOS恢复
    if (message_json.at("command").get<std::string>() == "sos-recovery") {
        RobotTaskCoordinator::instance().recover();
        return;
    }
    // 电池过热，退出充电桩
    if (message_json.at("command").get<std::string>() == "power-too-hot-return-charging-station") {
        battery_too_hot_handler.handle_async();
        return;
    }
}

void InternalCommandThread::rep_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto result = socket_rep.recv(zmq_message, zmq::recv_flags::none);
            if (!result.has_value()) continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("收到内部命令: {}", message);

            nlohmann::json message_json = nlohmann::json::parse(message);
            
            if (message_json.at("command") == "patrol_duty_uuid") {
                std::string task_uuid = RobotTaskCoordinator::instance().get_task_uuid();
                nlohmann::json response = {{"uuid", task_uuid}};
                socket_rep.send(zmq::buffer(response.dump()), zmq::send_flags::none);
                continue;
            }

            nlohmann::json response = {{"uuid", ""}};
            socket_rep.send(zmq::buffer(response.dump()), zmq::send_flags::none);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("内部命令异常: {}", e.what());
            nlohmann::json response = {{"uuid", ""}};
            socket_rep.send(zmq::buffer(response.dump()), zmq::send_flags::none);
        }
    }
}
