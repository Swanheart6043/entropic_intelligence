#include <ctime>
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "sole.hpp"
#include "battery_config_dao.hpp"
#include "../../common/utility.hpp"
#include "charge_target_monitor.hpp"

using namespace nlohmann;

ChargeTargetMonitor::ChargeTargetMonitor() {
    socket.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    socket.setsockopt(ZMQ_SUBSCRIBE, "power_info", strlen("power_info"));
    socket.connect("tcp://127.0.0.1:6060");
    
    socket_task_center.connect("tcp://127.0.0.1:4004");
    socket_internal_command.connect("tcp://127.0.0.1:8223");
    socket_web.connect("tcp://127.0.0.1:2007");
}

ChargeTargetMonitor::~ChargeTargetMonitor() {
    stop();
}

void ChargeTargetMonitor::start() {
    if (running) {
        SPDLOG_WARN("充电电量监控线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&ChargeTargetMonitor::run_loop, this);
}

void ChargeTargetMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("充电电量监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void ChargeTargetMonitor::run_loop() {
    auto last_target_check = std::chrono::steady_clock::now();

    while (running) {
        try {
            poll_and_handle_message();
            auto now = std::chrono::steady_clock::now();
            if (now - last_target_check >= std::chrono::minutes(5)) {
                last_target_check = now;
                check_charge_target();
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("充电监控循环异常: {}", e.what());
        }
    }
}

void ChargeTargetMonitor::poll_and_handle_message() {
    zmq::pollitem_t items[] = {{ static_cast<void*>(socket), 0, ZMQ_POLLIN, 0 }};
    zmq::poll(items, 1, std::chrono::milliseconds(500));
    if (!(items[0].revents & ZMQ_POLLIN)) return;

    zmq::message_t topic_msg, zmq_message;
    if (!socket.recv(topic_msg, zmq::recv_flags::none)) return;
    if (!socket.recv(zmq_message, zmq::recv_flags::none)) return;

    std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
    std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());

    if (topic == "power_info") {
        power_info = message;
        return;
    }
    if (topic == "charging_info") {
        charging_info = message;
        handle_charging_info(message);
    }
}

void ChargeTargetMonitor::handle_charging_info(const std::string& message) {
    bool is_charge = json::parse(message).at("is_auto_charge").get<bool>();
    if (last_is_charge.has_value() && last_is_charge.value() == is_charge) return;  // 状态没变

    const char* text = !last_is_charge.has_value() ? "unknown" : last_is_charge.value() ? "charging" : "standby";
    SPDLOG_INFO("充电状态变化: {} -> {}", text, is_charge ? "charging" : "standby");
    last_is_charge = is_charge;
    handle_charging_state_change(is_charge);
}

void ChargeTargetMonitor::handle_charging_state_change(bool is_charge) {
    if (is_charge) {
        handle_enter_charging();
        json message = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"CurrentTask", {
                {"TaskInfo", json::object()},
                {"TaskType", "charge"}
            }}
        };
        socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    } else {
        charging_complete_notified = false;  // 离开充电桩，复位
        json message = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"CurrentTask", {
                {"TaskInfo", json::object()},
                {"TaskType", "standBy"}
            }}
        };
        socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    }
}

void ChargeTargetMonitor::check_charge_target() {
    if (!last_is_charge.value_or(false)) return;   // 不在充电，无需检查
    if (charging_complete_notified) return;        // 已通知过，无需重复
    if (power_info.empty()) return;

    int battery = json::parse(power_info).at("battery_percentage").get<int>();
    auto battery_config = PowerConfigDao::get();
    SPDLOG_INFO("battery: {}, battery_high_pct: {}", battery, battery_config.battery_high_pct);

    if (battery >= battery_config.battery_high_pct) {
        charging_complete_notified = true;
        json message = {{"command", "charging-complete"}, {"content", ""}};
        socket_internal_command.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("通知任务中心，充电完成: {}", message.dump());
    }
}

void ChargeTargetMonitor::handle_enter_charging() {
    // 云台向后
    json ptz_message = {
        {"Header", {{"CmdType", "RC_Robot_PTZ_Ctrl"}}},
        {"Robot_PTZ_Ctrl", {{"unCtrlValue", "12"}, {"Value", "18"}}}
    };
    // 关大灯
    json head_light_message = {
        {"Header", {{"CmdType", "RC_RobotEnable_Set"}}},
        {"Switch", {{"Type", "14"}, {"Value", "0"}}}
    };
    // 关警灯
    json warning_light_message = {
        {"Header", {{"CmdType", "RC_RobotEnable_Set"}}},
        {"Switch", {{"Type", "12"}, {"Value", "0"}}}
    };
    // 关雨刮器
    json wiper_message = {
        {"Header", {{"CmdType", "RC_RobotEnable_Set"}}},
        {"Switch", {{"Type", "23"}, {"Value", "0"}}}
    };
    socket_task_center.send(zmq::buffer(ptz_message.dump()), zmq::send_flags::dontwait);
    socket_task_center.send(zmq::buffer(head_light_message.dump()), zmq::send_flags::dontwait);
    socket_task_center.send(zmq::buffer(warning_light_message.dump()), zmq::send_flags::dontwait);
    socket_task_center.send(zmq::buffer(wiper_message.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("通知任务中心云台向后, 关大灯, 关警灯, 关雨刷");
}