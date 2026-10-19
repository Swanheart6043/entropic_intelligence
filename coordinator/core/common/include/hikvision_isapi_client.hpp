#pragma once
#include "httplib.hpp"
#include <mutex>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace hik {

struct IsapiError : std::runtime_error {
    int  http_status;   // -1 表示底层连接失败
    int  isapi_code;    // ResponseStatus/statusCode；没解出来 = 0
    std::string body;

    IsapiError(int s, int c, std::string b, const std::string& m);
};

// PTZ 能力结构（从 capabilities XML 解析出来的常用字段）
struct PtzCapabilities {
    // 绝对变倍范围（单位 0.1×，比如 250 = 25.0×）
    int absolute_zoom_min = 0;
    int absolute_zoom_max = 0;

    // continuous pan/tilt 速度范围（常见 -100~100）
    int continuous_pan_min  = 0,  continuous_pan_max  = 0;
    int continuous_tilt_min = 0,  continuous_tilt_max = 0;
    // continuous zoom 速度范围
    int continuous_zoom_min = 0,  continuous_zoom_max = 0;

    // 功能位
    bool support_position3D = false;
    bool support_absoluteEx = false;
    bool support_wiper      = false;

    int max_preset_num  = 0;
    int max_patrol_num  = 0;
    int max_pattern_num = 0;

    std::string control_protocol;   // 当前协议：HIKVISION / PELCO-D / PELCO-P
    std::string control_address;    // RS485 地址

    // 倍率字符串（方便打印）
    double zoom_min_x() const { return absolute_zoom_min / 10.0; }
    double zoom_max_x() const { return absolute_zoom_max / 10.0; }
};

std::ostream& operator<<(std::ostream& os, const PtzCapabilities& c);

class HikvisionIsapiClient {
public:
    HikvisionIsapiClient(std::string host,
                         int         port,
                         std::string username,
                         std::string password,
                         int         channel = 1);

    // ====================================================
    //                  Continuous（速度模式）
    // ====================================================
    // pan / tilt / zoom 范围 [-100, 100]，全 0 = 停。
    void continuous_move(int pan, int tilt, int zoom);

    void stop();
    void zoom_in (int speed = 50);
    void zoom_out(int speed = 50);

    // ====================================================
    //              Absolute（精确角度 + 倍率）
    // ====================================================
    // azimuth_tenths    水平角，单位 0.1°，范围 [0, 3600]，对应 [0.0°, 360.0°]
    // elevation_tenths  俯仰角，单位 0.1°，范围 [-900, 2700]，对应 [-90.0°, 270.0°]
    // zoom_tenths       绝对倍率，单位 0.1×，对照 capabilities 的 absolute_zoom_min/max
    //
    // 注意：URL 是 /absolute，请求体 PTZData 内嵌 AbsoluteHigh 块。
    void set_absolute(int azimuth_tenths, int elevation_tenths, int zoom_tenths);

    // 便捷：只改倍率，pan/tilt 保持当前值（先 GET status 再 PUT absolute）
    void set_zoom_x(double zoom_x);

    // 获取当前 PTZ 状态 {azimuth_tenths, elevation_tenths, zoom_tenths}
    struct PtzStatus { int azimuth = 0; int elevation = 0; int absolute_zoom = 0; };
    PtzStatus get_status();

    // ====================================================
    //              Position3D（点击放大 / 框选）
    // ====================================================
    // 坐标 0~255，原点在【左下角】。把目标矩形左上 (x1,y1) 和右下 (x2,y2) 传进来，
    // 相机会自动转到这个矩形中心并放大。
    void position_3d(int x1, int y1, int x2, int y2);

    // 便捷：用归一化坐标 [0,1]（左下原点）框选放大
    void position_3d_norm(double x1, double y1, double x2, double y2);

    // ====================================================
    //                Zoom/Focus 原始坐标（机芯坐标系）
    // ====================================================
    void set_zoom_focus_position(int zoom_pos, int focus_pos);

    std::pair<int, int> get_zoom_focus_position();

    // ====================================================
    //                      Focus
    // ====================================================
    // focus 范围 [-100, 100]，负=近，正=远，0=停
    void focus_speed(int focus);

    enum class FocusMode { Auto, Manual, SemiAuto };
    void set_focus_mode(FocusMode mode);

    // ====================================================
    //                  Capabilities
    // ====================================================
    // 原始 XML（要看完整内容时用）
    std::string get_ptz_capabilities_raw();

    // 解析好的常用字段（推荐用这个）
    PtzCapabilities get_ptz_capabilities();

    // ====================================================
    // 通用裸接口
    // ====================================================
    std::string raw_get(const std::string& path);
    std::string raw_put(const std::string& path, const std::string& xml_body);

    // 提取 XML 标签内容的小工具（也能用来在子串中查找，做嵌套解析）
    static std::optional<std::string> extract_tag(const std::string& xml, const std::string& tag);

private:
    std::string host_;
    int port_;
    std::string user_;
    std::string pass_;
    int channel_;

    httplib::Client cli_;
    std::mutex mu_;

    std::string ptz_path(const std::string& suffix) const;

    std::string get_status_raw();

    static void clamp_speed(int& v);
    static void clamp_255(int& v);

    std::string put_xml(const std::string& path, const std::string& body);
    std::string get_xml(const std::string& path);
    std::string check(const httplib::Result& res, const std::string& path, const char* verb);
};

}