#include <ctime>
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "temperature_monitor.hpp"

TemperatureMonitor::TemperatureMonitor() {
    int timeout_ms = 1000;
    int linger = 0;
    socket_from_control.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    socket_from_control.setsockopt(ZMQ_SUBSCRIBE, "power_info", strlen("power_info"));
    socket_from_control.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_from_control.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_from_control.connect("tcp://127.0.0.1:6060");

    socket_to_task_center.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_to_task_center.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_to_task_center.connect("tcp://127.0.0.1:8223");
}

TemperatureMonitor::~TemperatureMonitor() {
    stop();
}

void TemperatureMonitor::start() {
    if (running) {
        SPDLOG_WARN("充电温度监控线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&TemperatureMonitor::temperature_loop, this);
}

void TemperatureMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("充电温度监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void TemperatureMonitor::temperature_loop() {
    using namespace nlohmann;
    using namespace std::chrono;

    bool is_charging = false;
    bool alarmed = false;                       // 本次充电已报警,不再重复
    steady_clock::time_point overheat_since{};  // 连续超温的起始时刻,epoch 表示"未超温"
    constexpr int kTempThreshold = 60;
    constexpr auto kConfirmDuration = seconds(5);

    while (running) {
        try {
            zmq::message_t topic_msg, body_msg;
            if (!socket_from_control.recv(topic_msg)) continue;  // 超时,回头检查 running
            if (!socket_from_control.recv(body_msg)) continue;

            std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
            std::string body(static_cast<char*>(body_msg.data()), body_msg.size());

            if (topic == "charging_info") {
                bool now_charging = json::parse(body).at("is_auto_charge").get<bool>();
                if (is_charging && !now_charging) {
                    // 充电结束,复位报警锁存和超温计时,下次充电重新监听
                    alarmed = false;
                    overheat_since = steady_clock::time_point{};
                }
                is_charging = now_charging;
                continue;
            }

            // 以下是 power_info
            if (!is_charging || alarmed) continue;

            int battery_temperature = json::parse(body).at("battery_temperature").get<int>();
            if (battery_temperature < kTempThreshold) {
                overheat_since = steady_clock::time_point{};  // 断了就重新计
                continue;
            }

            if (overheat_since == steady_clock::time_point{}) {
                overheat_since = steady_clock::now();  // 第一次超温,开始计时
                continue;
            }

            if (steady_clock::now() - overheat_since >= kConfirmDuration) {
                SPDLOG_WARN(
                    "充电中电池温度 {}°C 持续超过 {}s,触发过热处理",
                    battery_temperature, kConfirmDuration.count()
                );
                handle_battery_too_hot_during_charge();
                alarmed = true;
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("温度监听异常: {}", e.what());
        }
    }
}

void TemperatureMonitor::handle_battery_too_hot_during_charge() {
    nlohmann::json payload = {{"command", "power_too_hot_during_charging"}};
    socket_to_task_center.send(zmq::buffer(payload.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("充电时电池温度过高: {}", payload.dump());
}