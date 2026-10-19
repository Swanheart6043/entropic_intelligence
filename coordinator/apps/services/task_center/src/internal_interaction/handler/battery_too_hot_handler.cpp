#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "robot_task/charging/charging_point.hpp"
#include "component/mcu_controller.hpp"
#include "robot_task/robot_task_coordinator.hpp"
#include "battery_too_hot_handler.hpp"

BatteryTooHotHandler::BatteryTooHotHandler() {
	int linger = 1000;
	int sndtimeo = 1000; 
    int sndhwm = 100; 

    // 1秒，给消息足够的发送时间
    socket_to_edge_gateway.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
	// 设置send超时
    socket_to_edge_gateway.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
	// 设置发送高水位，防止接收方不在时消息堆积
    socket_to_edge_gateway.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    // 连接    
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
}

void BatteryTooHotHandler::handle_async() {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        SPDLOG_WARN("退桩正在进行中，忽略重复的过热退桩命令");
        return;
    }
    std::thread([] {
        try {
            BatteryTooHotHandler handler; // 实例在后台线程栈上，一个 ctx + 所有 socket
            handler.handle(); // 实例方法，能访问实例成员
        } catch (const std::exception& e) {
            SPDLOG_ERROR("过热退桩异步任务异常: {}", e.what());
        }
        // 重置
        running.store(false);
    }).detach();
}

void BatteryTooHotHandler::handle() {
    auto& coordinator = RobotTaskCoordinator::instance();
    coordinator.prepare_internal_motion();
    if (!coordinator.acquire_internal_motion(running)) {
        SPDLOG_WARN("过热退桩未获得机器人任务所有权");
        return;
    }
    struct OwnershipGuard {
        RobotTaskCoordinator& coordinator;
        ~OwnershipGuard() { coordinator.release_internal_motion(); }
    };
    OwnershipGuard ownership{coordinator};

    nlohmann::json message = {
        {"command", "broadcast-single-text"},
        {"content", "电池过热，开始退桩"}
    };
    auto send_result = socket_to_edge_gateway.send(
        zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    if (send_result) {
        SPDLOG_INFO("发送给边缘网关成功: {}", message.dump());
    } else {
        SPDLOG_WARN("发送给边缘网关失败 edge_gateway 可能未运行，消息已丢弃");
    }

    McuController mcu_controller;
    mcu_controller.release_emergency_stop();
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // 退桩 = 导航到充电点
    httplib::Client http{"http://10.10.10.10:8080"};
    if (charging::navigate_to_charging_point(http)) {
        SPDLOG_INFO("电池过热，退桩完成");
    } else {
        SPDLOG_WARN("电池过热退桩失败：导航到充电点未成功");
    }
}
