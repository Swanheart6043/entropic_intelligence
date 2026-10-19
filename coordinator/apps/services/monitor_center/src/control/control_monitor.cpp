#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "control_monitor.hpp"

McuMonitor::McuMonitor() {}

McuMonitor::~McuMonitor() {
    stop();
}

void McuMonitor::start() {
    if (running.exchange(true)) {
        SPDLOG_WARN("MCU监听线程, 已经在启动了");
        return;
    }
    // work_guard 让 io_ctx->run() 在没有待处理任务时也保持运行
    work_guard = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(*io_ctx));

    udp_io_thread = std::thread(&McuMonitor::udp_io_loop, this);
    receiver_forwarder_thread = std::thread(&McuMonitor::receiver_forwarder_loop, this);
    mcu_log_thread = std::thread(&McuMonitor::mcu_log_loop, this);
}

void McuMonitor::stop() {
    if (!running.exchange(false)) {
        SPDLOG_WARN("MCU监听线程没有启动, 无需停止");
        return;
    }
    if (receiver_forwarder_thread.joinable()) receiver_forwarder_thread.join();
    if (mcu_log_thread.joinable()) mcu_log_thread.join();

    // 只 reset work_guard，让 run() 把剩余任务（含关 socket）执行完后返回，
    // 不调 io_ctx->stop()，避免丢弃 pending handler
    if (work_guard) work_guard->reset();
    if (udp_io_thread.joinable()) udp_io_thread.join();

    SPDLOG_INFO("MCU监听线程已全部停止");
}

void McuMonitor::udp_io_loop() {
    while (running) {
        try {
            io_ctx->run();
            // run() 正常返回：要么 work_guard 被 reset 且无任务，要么 io_ctx->stop()
            SPDLOG_INFO("io_context 正常退出");
            break;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("io_context 异常, 2秒后重启: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(2));
            io_ctx->restart();
        }
    }
}

void McuMonitor::receiver_forwarder_loop() {
    CommunicationService communication_service(io_ctx);
    communication_service.start();
    while (running) std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void McuMonitor::mcu_log_loop() {
    McuLogService mcu_log_service(io_ctx);
    NavLogService nav_log_service(io_ctx);
    mcu_log_service.start();
    nav_log_service.start();
    while (running) std::this_thread::sleep_for(std::chrono::milliseconds(200));
}