#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "sole.hpp"
#include "config_parser.hpp"
#include "state_manager/state_manager.hpp"
#include "battery_monitor.hpp"
#include "battery_config_dao.hpp"
#include "../common/utility.hpp"

PowerMonitor::PowerMonitor() {
    zmq_socket_power.setsockopt(ZMQ_SUBSCRIBE, "power_info", strlen("power_info"));
    zmq_socket_power.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    zmq_socket_power.connect("tcp://127.0.0.1:6060");
    zmq_socket_web.connect("tcp://127.0.0.1:2007");
    socket_to_task_center.connect("tcp://127.0.0.1:8223");
}

PowerMonitor::~PowerMonitor() {
    stop();
}

void PowerMonitor::start() {
    if (running) {
        SPDLOG_WARN("电池监控线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&PowerMonitor::monitoring, this);
}

void PowerMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("电池监控线程没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void PowerMonitor::monitoring() {
    using namespace nlohmann;

    std::string power_info;
    std::string charging_info;
    std::mutex power_info_mtx;
    std::mutex charging_info_mtx;

    std::thread receive_thread = std::thread([&]{
        while (running) {
            try {
                zmq::message_t topic_msg, zmq_message;
                if (!zmq_socket_power.recv(topic_msg, zmq::recv_flags::none)) continue;
                if (!zmq_socket_power.recv(zmq_message, zmq::recv_flags::none)) continue;
                std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
                std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());

                if (topic == "power_info") {
                    std::lock_guard<std::mutex> lk(power_info_mtx);
                    power_info = message;
                } 
                if (topic == "charging_info") {
                    std::lock_guard<std::mutex> lk(charging_info_mtx);
                    charging_info = message;
                }
            } catch (const std::exception& e) {
                SPDLOG_ERROR("电源接收异常: {}", e.what());
            }
        }
    });

    std::thread send_thread = std::thread([&]{
        while (running) {
            try {
                std::string power_info_copy;
                std::string charging_info_copy;
                {
                    std::lock_guard<std::mutex> lk(power_info_mtx);
                    power_info_copy = power_info;
                }
                {
                    std::lock_guard<std::mutex> lk(charging_info_mtx);
                    charging_info_copy = charging_info;
                }
                if (power_info_copy.empty()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                if (charging_info_copy.empty()) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                
                int is_auto_charge = json::parse(charging_info_copy).at("is_auto_charge").get<bool>();
                int is_manual_charge = json::parse(charging_info_copy).at("is_manual_charge").get<bool>();
                battery = json::parse(power_info_copy).at("battery_percentage").get<int>();
                battery_temperature = json::parse(power_info_copy).at("battery_temperature").get<int>();
                charging = is_auto_charge || is_manual_charge;
                check_low_battery_warning();                
                check_low_battery_go_back_charge();
                check_battery_overheating();

                std::this_thread::sleep_for(std::chrono::minutes(5));
            } catch (const std::exception& e) {
                SPDLOG_ERROR("电源监控异常: {}", e.what());
            }
        }
    });
    send_thread.join();
    receive_thread.join();
}

void PowerMonitor::check_low_battery_warning() {
    auto battery_config = PowerConfigDao::get();
    if (battery <= battery_config.battery_low_pct && !charging) {
        sole::uuid id = sole::uuid4();
        std::string uuid_string = id.str();
        nlohmann::json message1 = {
            {"Header", {
                {"CmdType", "Robot_Alarm_info_Push"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"PatrolAlarmInfo", {
                {"time", format_now_date_to_string()},
                {"alarmType", 40100},
                {"alarmValue", "0.933"},
                {"uuid", uuid_string}
            }}
        };
        nlohmann::json report = {
            {"Header", {
                {"CmdType", "MCUStatusPost"}, 
                {"Sn", config->get_sn()}, 
                {"Time", std::time(nullptr)}
            }},
            {"MCUStatus", {
                {"ExceptionCode", "40100"}
            }}
        };
        zmq_socket_web.send(zmq::buffer(message1.dump()), zmq::send_flags::dontwait);
        zmq_socket_web.send(zmq::buffer(report.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("低电量告警: {}", message1.dump());
    }
}

void PowerMonitor::check_low_battery_go_back_charge() {
    auto is_auto = GlobalStateManager::getInstance().is_auto_mode();
    auto battery_config = PowerConfigDao::get();
    auto low = battery_config.battery_low_pct;

    // 低于最小阈值，且没在充电，且自动模式，且没触发过
    if (battery <= low && !charging && is_auto && !low_battery_charge_triggered) {
        low_battery_charge_triggered = true;
        nlohmann::json message = {{"command", "low_power_go_to_charge"}};
        socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("电量低, 通知任务中心: {}/{}", battery, low);
    }
    // 充电后电量恢复，重置标志
    if (charging && battery > low) {
        SPDLOG_INFO("充电达到最低阈值");
        low_battery_charge_triggered = false;
    }
}

// 检查电池温度过高
void PowerMonitor::check_battery_overheating() {
    auto cfg = PowerConfigDao::get();
    if (battery_temperature >= cfg.battery_temp_warn_c) {
        nlohmann::json report = {
            {"Header", {
                {"CmdType", "MCUStatusPost"}, 
                {"Sn", config->get_sn()}, 
                {"Time", std::time(nullptr)}
            }},
            {"MCUStatus", {{"ExceptionCode", "40102"}}}
        };
        zmq_socket_web.send(zmq::buffer(report.dump()), zmq::send_flags::dontwait);
    }
}