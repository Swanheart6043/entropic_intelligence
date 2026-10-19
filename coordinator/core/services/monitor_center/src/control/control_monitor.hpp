#pragma once
#include <thread>
#include <atomic>
#include <memory>
#include <zmq.hpp>
#include <boost/asio.hpp>
#include "config_parser.hpp"
#include "./common/udp_client.hpp"
#include "./core/core_thread.hpp"
#include "./logger/mcu_log_service.hpp"
#include "./logger/nav_log_service.hpp"

class McuMonitor {
public:
    McuMonitor();
    ~McuMonitor();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    std::thread udp_io_thread;
    std::thread receiver_forwarder_thread;
    std::thread mcu_log_thread;

    std::shared_ptr<boost::asio::io_context> io_ctx = std::make_shared<boost::asio::io_context>();
    // work_guard 持有，防止 io_ctx 在没任务时提前返回；stop 时 reset 让 run() 退出
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard;

    void udp_io_loop();
    void receiver_forwarder_loop();
    void mcu_log_loop();
};