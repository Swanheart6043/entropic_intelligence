#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <iomanip>
#include "config_parser.hpp"
#include "hikvision_isapi_client.hpp"

using u8 = uint8_t;
using Frame = std::vector<u8>;

enum class WiperMode {
    OFF, // 关闭
    SINGLE_BRUSH, // 单刷
    CONTINUOUS_BRUSH, // 连刷
};

class PTZController {
public:
    explicit PTZController();
    ~PTZController();
    // 开串口
    int open_serial();
    // 关串口
    void close_serial();
    // 匹配云台控制
    void match_ptz_control(const std::string& message);
    // 上
    bool to_up();
    // 下
    bool to_down();
    // 左
    bool to_left();
    // 右
    bool to_right();
    // 复位
    void reset_ptz();
    // 停
    bool stop();
    // 放大
    void zoom_in();
    // 缩小
    void zoom_out();
    // 设置 5 倍
    void zoom_to();
    // 回到 1 倍
    void zoom_reset();
    // 匹配雨刷控制
    void match_wiper_control(const std::string& message);
    // 开雨刷
    void turn_on_wiper(WiperMode);
    // 关雨刷
    void turn_off_wiper(WiperMode);
    // 查询水平角度
    bool query_pan(double &out_angle);
    // 查询垂直角度
    bool query_tilt(double &out_angle);
    // 设置水平角度
    bool set_pan_abs(double angle_deg);
    // 设置垂直角度
    bool set_tilt_abs(double angle_deg);
    // 执行预置位
    void execute_preset_position(const std::string& code);

private:
    hik::HikvisionIsapiClient cam{"10.10.10.40", 80, "admin", "heyn_888", 1};
    Config* config = Config::getInstance();
    u8 addr = 0x01;
    int fd;

    bool send_frame(int fd, const Frame &f);
    bool read_frame(int fd, Frame &out, int timeout_seconds=1);
    Frame build_base_frame(u8 addr, u8 cmd1, u8 cmd2, u8 data1, u8 data2);
    u8 checksum5(const Frame &f);
};