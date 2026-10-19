#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "bootstrap.hpp"
#include "config_parser.hpp"
#include "control/control_monitor.hpp"
#include "perceiver/perceiver_monitor.hpp"
#include "state_manager/state_manager.hpp"
#include "battery/battery_monitor.hpp"
#include "button/button_monitor.hpp"
#include "charging/charging_monitor.hpp"
#include "navigator/location_monitor.hpp"
#include "navigator/stay_too_long_monitor.hpp"
#include "navigator/exception_codes_monitor.hpp"
#include "navigator/obstacle_monitor.hpp"

int main() {
    try {
        patrol_robot::init_crash_handler("monitor_center");
        patrol_robot::init_logging("monitor_center");
        Config* config = Config::getInstance();
        config->loadFromFile("/etc/patrol_robot_h1/config.ini");

        GlobalStateManager::getInstance().start();
        PowerMonitor battery_monitor;
        ChargingMonitor charging_monitor;
        LocationMonitor location_monitor;
        StayMonitor stay_monitor;
        ButtonMonitor button_monitor;
        LiDarMonitor li_dar_monitor;
        ObstacleMonitor obstacle_monitor;
        McuMonitor mcu_monitor;
        PerceiverCenterServer perceiver_monitor;
        
        battery_monitor.start();
        charging_monitor.start();
        location_monitor.start();
        stay_monitor.start();
        button_monitor.start();
        li_dar_monitor.start();
        obstacle_monitor.start();
        mcu_monitor.start();
        perceiver_monitor.start();
        
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("监控中心异常: {}", e.what());
        return -1;
    }
}