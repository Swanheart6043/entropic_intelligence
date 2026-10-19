#pragma once
#include <iostream>
#include <string>
#include <zmq.hpp>
#include "config_parser.hpp"

class McuController {
public:   
    explicit McuController();
    // 解除急停
    void release_emergency_stop();
    // 开大灯
    void turn_on_head_light();
    // 关大灯
    void turn_off_head_light();
    // 开警灯
    void turn_on_warning_light();
    // 关警灯
    void turn_off_warning_light();
    // 取消充电(MCU层的取消充电不会退桩，需要退桩得调用对桩控制器中的方法)
    void cancel_charging();
    // 处理关机前事项
    void handle_before_shutdown();
    // 重启导航
    void reboot_navigation();
    // 重启工控机
    void reboot_industrial_computer();
    // 重启主控板
    void reboot_power();
    // 关机
    void shutdown();

private:
    Config* config = Config::getInstance();
    zmq::context_t zmq_ctx{1};
    zmq::socket_t zmq_socket_web{zmq_ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_power{zmq_ctx, zmq::socket_type::push};
};