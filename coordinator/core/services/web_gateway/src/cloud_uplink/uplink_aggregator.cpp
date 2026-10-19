#include <iostream>
#include <filesystem>
#include <regex>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "config_parser.hpp"
#include "common/reporter_logger.hpp"
#include "uplink_aggregator.hpp"

CloudUplinkAggregator::CloudUplinkAggregator(WebSocketClient& websocket_connect) : websocket(websocket_connect) {
    const std::string log_dir = "/home/heyn/patrol_robot_h1/logs/web_gateway";
    const std::string log_path = log_dir + "/sender.log";
    
    clear_old_logs(log_dir, 5);
	create_log(log_path);

    socket_platform.bind("tcp://*:2007");
}

CloudUplinkAggregator::~CloudUplinkAggregator() {
    stop();
}

void CloudUplinkAggregator::start() {
    if (worker_thread.joinable()) {
        SPDLOG_WARN("发送线程已在运行中");
        return;
    }
    worker_thread = std::thread(&CloudUplinkAggregator::listen_processes, this);
}

void CloudUplinkAggregator::stop() {
    if (worker_thread.joinable()) worker_thread.join();
    if (logger) logger->flush();
}

void CloudUplinkAggregator::listen_processes() {
    while (true) {
        try {
            zmq::message_t zmq_message;
            auto zmq_result = socket_platform.recv(zmq_message, zmq::recv_flags::none);
            if (!zmq_result.has_value()) continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            
            if (message.find("ExceptionCode") != std::string::npos) {
                logger->info("异常码信息: {}", message);
                websocket.send_message(message);
                continue;
            }
            if (message.find("Robot_Alarm_info_Push") != std::string::npos) {
                logger->info("告警信息: {}", message);
                websocket.send_message(message);
                continue;
            }
            if (message.find("NavStatusPost") != std::string::npos) {
                logger->info("位置信息: {}", message);
                websocket.send_message(message);
                continue;
            }
            if (message.find("PhoenixStatusPost") != std::string::npos) {
                logger->info("运动信息: {}", message);
                websocket.send_message(message);
                continue;
            }
            if (message.find("Robot_Capture_info_Push") != std::string::npos) {
                logger->info("抓拍信息: {}", message);
                websocket.send_message(message);
                continue;
            }
            
            SPDLOG_INFO("收到其他进程消息: {}", message);
            websocket.send_message(message);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("监听其他进程消息异常: {}", e.what());
        }
    }
}

void CloudUplinkAggregator::clear_old_logs(const std::string& log_dir, int max_day) {
	namespace fs = std::filesystem;
    
    if (!fs::exists(log_dir)) return;
    
    // 关键：使用 file_time_type 的时钟，而不是 system_clock
    auto now = fs::file_time_type::clock::now();
    std::regex log_pattern(R"(mcu_\d{4}-\d{2}-\d{2}\.log)");
    
    for (const auto& entry : fs::directory_iterator(log_dir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, log_pattern)) continue;
        
        auto file_time = fs::last_write_time(entry);
        // 直接相减，无需 clock_cast
        auto file_age = now - file_time;
        auto hours_old = std::chrono::duration_cast<std::chrono::hours>(file_age).count();
        int days_old = static_cast<int>(hours_old / 24);
        
        if (days_old > max_day) {
            SPDLOG_INFO("删除过期日志: {} ({}天前)", filename, days_old);
            std::error_code ec;
            fs::remove(entry.path(), ec);
            if (ec) {
                SPDLOG_WARN("删除失败: {} - {}", filename, ec.message());
            }
        }
    }
}

void CloudUplinkAggregator::create_log(const std::string& log_path) {
	auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
		log_path,
		0, // 轮转时间：小时 (0 = 午夜)
		0, // 轮转时间：分钟
		false, // 不截断现有文件
		14 // 最大保留文件数
	);
	logger = std::make_shared<spdlog::logger>("mcu", sink);
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
	logger->flush_on(spdlog::level::info);
}