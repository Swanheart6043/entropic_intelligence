#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "button_monitor.hpp"

ButtonMonitor::ButtonMonitor() {
    zmq_socket_power.setsockopt(ZMQ_SUBSCRIBE, "button_info", strlen("button_info"));
    zmq_socket_power.connect("tcp://127.0.0.1:6060");
    zmq_socket_web.connect("tcp://127.0.0.1:2007");
}

ButtonMonitor::~ButtonMonitor() {
    stop();
}

void ButtonMonitor::start() {
    if (running) {
        SPDLOG_WARN("任务中心监听线程，已经在启动了");
        return;
    }
    running = true;
    sos_button_thread = std::thread(&ButtonMonitor::sos_loop, this);
    stop_button_thread = std::thread(&ButtonMonitor::stop_loop, this);
    power_button_thread = std::thread(&ButtonMonitor::power_loop, this);
}

void ButtonMonitor::stop() {
    if (!running) return;
    running = false;
    if (sos_button_thread.joinable()) sos_button_thread.join();
    if (stop_button_thread.joinable()) stop_button_thread.join();
    if (power_button_thread.joinable()) power_button_thread.join();
}

void ButtonMonitor::sos_loop() {
    SosButtonMonitor sos_button_monitor;
    sos_button_monitor.start();
}

void ButtonMonitor::stop_loop() {
    StopButtonMonitor stop_button_monitor;
    stop_button_monitor.start();
}

void ButtonMonitor::power_loop() {
    PowerButtonMonitor power_button_monitor;
    power_button_monitor.start();
}