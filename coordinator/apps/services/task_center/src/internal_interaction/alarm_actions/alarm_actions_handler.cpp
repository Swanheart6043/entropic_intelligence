#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "robot_task/robot_task_coordinator.hpp"
#include "alarm_actions_handler.hpp"

AlarmActionHandler::AlarmActionHandler() {
    int linger = 1000;
	int sndtimeo = 1000; 
    int sndhwm = 100; 
    socket_to_edge_gateway.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_to_edge_gateway.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    socket_to_edge_gateway.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
}

AlarmActionHandler::~AlarmActionHandler() {

}

void AlarmActionHandler::handle_async(const std::string& message) {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        SPDLOG_WARN("告警动作正在进行中，忽略重复的告警动作命令");
        return;
    }
    std::thread([message] {
        try {
            AlarmActionHandler handler; // 实例在后台线程栈上，一个 ctx + 所有 socket
            handler.handle(message); // 实例方法，能访问实例成员
        } catch (const std::exception& e) {
            SPDLOG_ERROR("告警动作异步任务异常: {}", e.what());
        }
        // 重置
        running.store(false);
    }).detach();
}

void AlarmActionHandler::handle(const std::string& message) {
    std::vector<AlarmAction> actions;
    try {
        auto message_json = nlohmann::json::parse(message);
        auto action_list = message_json.value("action_list", nlohmann::json::array());
        actions = action_list.get<std::vector<AlarmAction>>();
    } catch (const nlohmann::json::exception& e) {
        SPDLOG_ERROR("解析 alarm_actions 失败: {}", e.what());
        return;
    }
    for (const auto& action : actions) {
        // 停止运动
        if (action.action_type == "111") {
            // handle_pause_move(action);
            continue;
        }
        // 恢复运动
        if (action.action_type == "112") { 
            // handle_resume_move(action);
            continue;
        }
        // SOS
        if (action.action_type == "110") {
            // handle_sos(action);
            continue;
        }
        // 声音播报
        if (action.action_type == "8") {           
            handle_broadcasts(action);
            continue;
        } 
        // 警灯控制
        if (action.action_type == "32") { 
            handle_warning_light(action);
            continue;
        }
        // 环视
        if (action.action_type == "11") {
            // handle_looking_around(action);
            continue;
        }
        SPDLOG_WARN("未知动作类型: {}", action.action_type);
    }
}

void AlarmActionHandler::handle_broadcasts(AlarmAction action) {
    static const std::unordered_map<std::string, std::string> alarm_map = {
        {"488", "smoking"}, // 吸烟
        {"426", "no_safety_helmet"}, // 未戴安全帽
        {"1023", "no_reflective_vest"}, // 未穿反光衣
        {"10016", "rubbish"}, // 地面垃圾
        {"223", "thermal_overtemp"}, // 红外高温
        {"441", "pet"}, // 宠物
        {"311", "fire"}, // 火灾
        {"10008", "ground_water"}, // 地面积水
    };
    auto it = alarm_map.find(action.alarm_type);
    auto type = it != alarm_map.end() ? it->second : "interchangeable";
    
    nlohmann::json params = {
        {"command", "broadcast-single-text"},
        {"content", action.value2}
    };
    socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);

    nlohmann::json payload = {
        {"command", "alarm"},
        {"type", type},
        {"content", action.value2}
    };
    socket_to_edge_gateway.send(zmq::buffer(payload.dump()), zmq::send_flags::dontwait);
    sleeping(action.wait_ms);
}

void AlarmActionHandler::handle_warning_light(AlarmAction action) {
    if (action.value1 == "1") mcu_controller.turn_on_warning_light();
    if (action.value1 == "2") mcu_controller.turn_off_warning_light();
    sleeping(action.wait_ms);
}

void AlarmActionHandler::handle_looking_around(AlarmAction action) {
    ptz_controller.to_left();
    sleeping(action.wait_ms);
}

void AlarmActionHandler::handle_sos(AlarmAction action) {
    RobotTaskCoordinator::instance().cancel();
    sleeping(action.wait_ms);
}

void AlarmActionHandler::handle_pause_move(AlarmAction action) {
    RobotTaskCoordinator::instance().cancel();
    sleeping(action.wait_ms);
}

void AlarmActionHandler::handle_resume_move(AlarmAction action) {
    RobotTaskCoordinator::instance().recover();
    sleeping(action.wait_ms);
}

void AlarmActionHandler::sleeping(int ms) {
    if (ms <= 0) return;
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, std::chrono::milliseconds(ms), [this]() {
        return !running.load();
    });
}

void AlarmActionHandler::cancel() {
    
}

void AlarmActionHandler::resume() {
    
}

bool AlarmActionHandler::is_progress() {
    return true;
}
