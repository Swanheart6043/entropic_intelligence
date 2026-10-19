#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "httplib.hpp"
#include "stay_too_long_monitor.hpp"

StayMonitor::StayMonitor() {
    socket_power.setsockopt(ZMQ_SUBSCRIBE, "system_status", strlen("system_status"));
    socket_power.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    socket_power.connect("tcp://127.0.0.1:6060");
    socket_web.connect("tcp://127.0.0.1:2007");
}

StayMonitor::~StayMonitor() {
    stop();
}

void StayMonitor::start() {
    if (running) {
        SPDLOG_WARN("停留监控线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&StayMonitor::monitoring, this);
}

void StayMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("停留监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void StayMonitor::monitoring() {
    while (true) {
        try {
            // 发送端300毫秒发送一次
            zmq::message_t topic_msg, zmq_message;
            auto topic_result = socket_power.recv(topic_msg, zmq::recv_flags::none);
            if (!topic_result.has_value()) continue;
            auto message_result = socket_power.recv(zmq_message, zmq::recv_flags::none);
            if (!message_result.has_value()) continue;
            
            std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
            if (topic != "system_status" && topic != "charging_info") continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            if (message.empty()) continue;

            if (topic == "charging_info") {
                auto json_data = nlohmann::json::parse(message);
                is_auto_charging = json_data.at("is_auto_charge").get<bool>();
                is_manual_charging = json_data.at("is_manual_charge").get<bool>();
            }

            if (topic == "system_status") {
                auto json_data = nlohmann::json::parse(message);

                // 字段不存在时跳过，避免误判
                if (!json_data.contains("is_run")) {
                    SPDLOG_DEBUG("system_status 缺少 is_run 字段，跳过");
                    continue;
                }

                is_run = json_data.at("is_run").get<bool>();
                
                // 如果超过5分钟没有动且没充电，上报长时间滞留
                if (!is_run && !is_auto_charging && !is_manual_charging) {
                    handle_long_term_stays();
                    continue;
                }
                handle_not_long_term_stays();
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("长时间滞留监控异常: {}", e.what());
        }
    }
}

void StayMonitor::handle_long_term_stays() {
    // 如果之前没有在停留，开始记录停留时间
    if (!is_staying) {
        stay_start_time = std::chrono::steady_clock::now();
        last_report_time = {}; // 重置上报时间
        is_staying = true;
        return;
    } 
    // 已经在停留中，计算停留时长
    auto now = std::chrono::steady_clock::now();
    auto stay_duration = std::chrono::duration_cast<std::chrono::seconds>(now - stay_start_time).count();
    // 超过5分钟
    if (stay_duration >= STAY_THRESHOLD_SECONDS) {
        auto since_last_report = std::chrono::duration_cast<std::chrono::seconds>(now - last_report_time).count();
        // 首次上报 或 距离上次上报超过间隔
        bool is_first_report = (last_report_time == std::chrono::steady_clock::time_point{});
        if (is_first_report || since_last_report >= REPORT_INTERVAL_SECONDS) {
            nlohmann::json report = {
                {"Header", {
                    {"CmdType", "MCUStatusPost"},
                    {"Sn", config->get_sn()},
                    {"Time", std::time(nullptr)}
                }},
                {"MCUStatus", {
                    {"ExceptionCode", "50103"}
                }}
            };
            socket_web.send(zmq::buffer(report.dump()), zmq::send_flags::dontwait);
            last_report_time = now;
            SPDLOG_WARN("长时间滞留上报: 停留时长{}秒, {}", stay_duration, report.dump());
        }
    } else {
        // 还没到5分钟，可以定期打印进度（可选）
        if (stay_duration % 60 == 0 && stay_duration > 0) {  
            // 每分钟打印一次
            SPDLOG_INFO("停留中，已停留{}秒/{}秒", stay_duration, STAY_THRESHOLD_SECONDS);
        }
    }
}

void StayMonitor::handle_not_long_term_stays() {
    if (!is_staying) return;
    // 不再满足停留条件（开始运行或开始充电），重置状态
    auto time_range = std::chrono::steady_clock::now() - stay_start_time;
    auto stay_duration = std::chrono::duration_cast<std::chrono::seconds>(time_range).count();
    std::string text = is_run ? "开始运行" : "正在充电";
    SPDLOG_INFO("停留结束，停留时长: {}秒, 原因: {}", stay_duration, text);
    is_staying = false;
}
