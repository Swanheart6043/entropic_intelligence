#include <spdlog/spdlog.h>
#include "charging_monitor.hpp"

ChargingMonitor::ChargingMonitor() {
    
}

ChargingMonitor::~ChargingMonitor() {
    stop();
}

void ChargingMonitor::start() {
    if (running) {
        SPDLOG_WARN("充电监控模块，已经在启动了");
        return;
    }
    running = true;
    charge_target_monitor.start();
    time_synchronizer.start();
    temperature_monitor.start();
}

void ChargingMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("充电监控模块，没有启动，无需停止");
        return;
    }
    running = false;
    charge_target_monitor.stop();
    time_synchronizer.stop();
    temperature_monitor.stop();
}