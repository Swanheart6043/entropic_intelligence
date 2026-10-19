#include "hikvision_isapi_client.hpp"
#include <chrono>
#include <cstdlib> // std::abs

namespace hik {

// ========================================================
//                      IsapiError
// ========================================================
IsapiError::IsapiError(int s, int c, std::string b, const std::string& m) : std::runtime_error(m), http_status(s), isapi_code(c), body(std::move(b)) {

}

// ========================================================
//                  PtzCapabilities 打印
// ========================================================
std::ostream& operator<<(std::ostream& os, const PtzCapabilities& c) {
    os << "PTZ capabilities:\n"
       << "zoom range: " << c.zoom_min_x() << "x ~ " << c.zoom_max_x() << "x  "
       << "(absolute_zoom" << c.absolute_zoom_min << "~" << c.absolute_zoom_max << ")\n"
       << "pan speed: [" << c.continuous_pan_min  << ", " << c.continuous_pan_max  << "]\n"
       << "tilt speed: [" << c.continuous_tilt_min << ", " << c.continuous_tilt_max << "]\n"
       << "zoom speed: [" << c.continuous_zoom_min << ", " << c.continuous_zoom_max << "]\n"
       << "position3D: " << (c.support_position3D ? "yes" : "no") << "\n"
       << "absoluteEx: " << (c.support_absoluteEx ? "yes" : "no") << "\n"
       << "wiper: " << (c.support_wiper ? "yes" : "no") << "\n"
       << "max preset: " << c.max_preset_num << "\n"
       << "protocol: " << c.control_protocol
       << "(addr " << c.control_address << ")\n";
    return os;
}

// ========================================================
//                  HikvisionIsapiClient
// ========================================================
HikvisionIsapiClient::HikvisionIsapiClient(std::string host, int port, std::string username, std::string password, int channel) : host_(std::move(host)), port_(port), user_(std::move(username)), pass_(std::move(password)), channel_(channel), cli_(host_, port_) {
    cli_.set_digest_auth(user_.c_str(), pass_.c_str());
    cli_.set_connection_timeout(std::chrono::seconds(3));
    cli_.set_read_timeout(std::chrono::seconds(5));
    cli_.set_write_timeout(std::chrono::seconds(5));
    cli_.set_keep_alive(true);
}

// ----------------------- Continuous -----------------------
void HikvisionIsapiClient::continuous_move(int pan, int tilt, int zoom) {
    clamp_speed(pan); clamp_speed(tilt); clamp_speed(zoom);
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<PTZData version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<pan>" + std::to_string(pan) + "</pan>"
        "<tilt>" + std::to_string(tilt) + "</tilt>"
        "<zoom>" + std::to_string(zoom) + "</zoom>"
        "</PTZData>";
    put_xml(ptz_path("/continuous"), body);
}

void HikvisionIsapiClient::stop() { 
    continuous_move(0, 0, 0); 
}

void HikvisionIsapiClient::zoom_in (int speed) { 
    continuous_move(0, 0, std::abs(speed));
}

void HikvisionIsapiClient::zoom_out(int speed) { 
    continuous_move(0, 0, -std::abs(speed)); 
}

// ----------------------- Absolute -----------------------
void HikvisionIsapiClient::set_absolute(int azimuth_tenths, int elevation_tenths, int zoom_tenths) {
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<PTZData version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<AbsoluteHigh>"
          "<elevation>" + std::to_string(elevation_tenths) + "</elevation>"
          "<azimuth>" + std::to_string(azimuth_tenths) + "</azimuth>"
          "<absoluteZoom>" + std::to_string(zoom_tenths) + "</absoluteZoom>"
        "</AbsoluteHigh>"
        "</PTZData>";
    put_xml(ptz_path("/absolute"), body);
}

void HikvisionIsapiClient::set_zoom_x(double zoom_x) {
    auto st = get_status_raw();
    int az = std::stoi(extract_tag(st, "azimuth").value_or("0"));
    int el = std::stoi(extract_tag(st, "elevation").value_or("0"));
    int zt = static_cast<int>(zoom_x * 10 + 0.5); // 5.0× -> 50
    set_absolute(az, el, zt);
}

HikvisionIsapiClient::PtzStatus HikvisionIsapiClient::get_status() {
    auto xml = get_status_raw();
    PtzStatus s;
    s.azimuth = std::stoi(extract_tag(xml, "azimuth").value_or("0"));
    s.elevation = std::stoi(extract_tag(xml, "elevation").value_or("0"));
    s.absolute_zoom = std::stoi(extract_tag(xml, "absoluteZoom").value_or("0"));
    return s;
}

// ----------------------- Position3D -----------------------
void HikvisionIsapiClient::position_3d(int x1, int y1, int x2, int y2) {
    clamp_255(x1); clamp_255(y1); clamp_255(x2); clamp_255(y2);
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Position3D version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<StartPoint>"
          "<positionX>" + std::to_string(x1) + "</positionX>"
          "<positionY>" + std::to_string(y1) + "</positionY>"
        "</StartPoint>"
        "<EndPoint>"
          "<positionX>" + std::to_string(x2) + "</positionX>"
          "<positionY>" + std::to_string(y2) + "</positionY>"
        "</EndPoint>"
        "</Position3D>";
    // 注意 URL 是大写 C 的 Channels
    put_xml("/ISAPI/PTZCtrl/Channels/" + std::to_string(channel_) + "/position3D", body);
}

void HikvisionIsapiClient::position_3d_norm(double x1, double y1, double x2, double y2) {
    position_3d(
        static_cast<int>(x1 * 255 + 0.5),
        static_cast<int>(y1 * 255 + 0.5),
        static_cast<int>(x2 * 255 + 0.5),
        static_cast<int>(y2 * 255 + 0.5)
    );
}

// ----------------------- Zoom/Focus 原始坐标 -----------------------
void HikvisionIsapiClient::set_zoom_focus_position(int zoom_pos, int focus_pos) {
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<ZoomFocus version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<pqrsZoom>" + std::to_string(zoom_pos)  + "</pqrsZoom>"
        "<mnstFocus>" + std::to_string(focus_pos) + "</mnstFocus>"
        "</ZoomFocus>";
    put_xml(ptz_path("/zoomFocus"), body);
}

std::pair<int, int> HikvisionIsapiClient::get_zoom_focus_position() {
    auto xml = get_xml(ptz_path("/zoomFocus"));
    return {
        std::stoi(extract_tag(xml, "pqrsZoom").value_or("0")),
        std::stoi(extract_tag(xml, "mnstFocus").value_or("0"))
    };
}

// ----------------------- Focus -----------------------
void HikvisionIsapiClient::focus_speed(int focus) {
    clamp_speed(focus);
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<FocusData version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<focus>" + std::to_string(focus) + "</focus>"
        "</FocusData>";
    put_xml("/ISAPI/System/Video/inputs/channels/" + std::to_string(channel_) + "/focus", body);
}

void HikvisionIsapiClient::set_focus_mode(FocusMode mode) {
    const char* s = "AUTO";
    switch (mode) {
        case FocusMode::Manual:   s = "MANUAL"; break;
        case FocusMode::SemiAuto: s = "SEMIAUTOMATIC"; break;
        case FocusMode::Auto:     s = "AUTO"; break;
    }
    const std::string body =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<FocusConfiguration version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
        "<focusStyle>" + std::string(s) + "</focusStyle>"
        "</FocusConfiguration>";
    put_xml("/ISAPI/Image/channels/" + std::to_string(channel_) + "/focusConfiguration", body);
}

// ----------------------- Capabilities -----------------------
std::string HikvisionIsapiClient::get_ptz_capabilities_raw() {
    return get_xml(ptz_path("/capabilities"));
}

PtzCapabilities HikvisionIsapiClient::get_ptz_capabilities() {
    auto xml = get_ptz_capabilities_raw();
    PtzCapabilities c;

    // <AbsoluteZoomPositionSpace><ZRange><Min>10</Min><Max>250</Max>...
    if (auto blk = extract_tag(xml, "AbsoluteZoomPositionSpace")) {
        if (auto zr = extract_tag(*blk, "ZRange")) {
            c.absolute_zoom_min = std::stoi(extract_tag(*zr, "Min").value_or("0"));
            c.absolute_zoom_max = std::stoi(extract_tag(*zr, "Max").value_or("0"));
        }
    }
    // <ContinuousPanTiltSpace><XRange><Min>-100</Min><Max>100</Max>...<YRange>...
    if (auto blk = extract_tag(xml, "ContinuousPanTiltSpace")) {
        if (auto xr = extract_tag(*blk, "XRange")) {
            c.continuous_pan_min = std::stoi(extract_tag(*xr, "Min").value_or("0"));
            c.continuous_pan_max = std::stoi(extract_tag(*xr, "Max").value_or("0"));
        }
        if (auto yr = extract_tag(*blk, "YRange")) {
            c.continuous_tilt_min = std::stoi(extract_tag(*yr, "Min").value_or("0"));
            c.continuous_tilt_max = std::stoi(extract_tag(*yr, "Max").value_or("0"));
        }
    }
    // <ContinuousZoomSpace><ZRange>...
    if (auto blk = extract_tag(xml, "ContinuousZoomSpace")) {
        if (auto zr = extract_tag(*blk, "ZRange")) {
            c.continuous_zoom_min = std::stoi(extract_tag(*zr, "Min").value_or("0"));
            c.continuous_zoom_max = std::stoi(extract_tag(*zr, "Max").value_or("0"));
        }
    }

    c.support_position3D = (extract_tag(xml, "isSupportPosition3D").value_or("false") == "true");
    c.support_absoluteEx = (extract_tag(xml, "isSupportAbsoluteEx").value_or("false") == "true");
    c.support_wiper      = (extract_tag(xml, "wiperStatusSupport") .value_or("false") == "true");

    c.max_preset_num  = std::stoi(extract_tag(xml, "maxPresetNum") .value_or("0"));
    c.max_patrol_num  = std::stoi(extract_tag(xml, "maxPatrolNum") .value_or("0"));
    c.max_pattern_num = std::stoi(extract_tag(xml, "maxPatternNum").value_or("0"));

    c.control_protocol = extract_tag(xml, "controlProtocol").value_or("");
    c.control_address  = extract_tag(xml, "controlAddress") .value_or("");

    return c;
}

// ----------------------- 通用裸接口 -----------------------
std::string HikvisionIsapiClient::raw_get(const std::string& path) {
    return get_xml(path);
}

std::string HikvisionIsapiClient::raw_put(const std::string& path, const std::string& xml_body) {
    return put_xml(path, xml_body);
}

// ----------------------- XML 小工具 -----------------------
std::optional<std::string> HikvisionIsapiClient::extract_tag(const std::string& xml, const std::string& tag) {
    const std::string open  = "<" + tag + ">";
    const std::string close = "</" + tag + ">";
    auto a = xml.find(open);
    if (a == std::string::npos) {
        // 兼容 <tag attr="..."> 这种
        const std::string open_attr = "<" + tag + " ";
        a = xml.find(open_attr);
        if (a == std::string::npos) return std::nullopt;
        a = xml.find('>', a);
        if (a == std::string::npos) return std::nullopt;
        // 处理 <tag attr="..."/> 自闭合
        if (a > 0 && xml[a-1] == '/') return std::string{};
        a += 1;
    } else {
        a += open.size();
    }
    auto b = xml.find(close, a);
    if (b == std::string::npos) return std::nullopt;
    return xml.substr(a, b - a);
}

// ----------------------- private -----------------------
std::string HikvisionIsapiClient::ptz_path(const std::string& suffix) const {
    return "/ISAPI/PTZCtrl/channels/" + std::to_string(channel_) + suffix;
}

std::string HikvisionIsapiClient::get_status_raw() { 
    return get_xml(ptz_path("/status")); 
}

void HikvisionIsapiClient::clamp_speed(int& v) {
    if (v >  100) v =  100;
    if (v < -100) v = -100;
}

void HikvisionIsapiClient::clamp_255(int& v) {
    if (v > 255) v = 255;
    if (v < 0)   v =   0;
}

std::string HikvisionIsapiClient::put_xml(const std::string& path, const std::string& body) {
    std::lock_guard<std::mutex> lk(mu_);
    auto res = cli_.Put(path.c_str(), body, "application/xml; charset=\"UTF-8\"");
    return check(res, path, "PUT");
}

std::string HikvisionIsapiClient::get_xml(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    auto res = cli_.Get(path.c_str());
    return check(res, path, "GET");
}

std::string HikvisionIsapiClient::check(const httplib::Result& res, const std::string& path, const char* verb) {
    if (!res) {
        throw IsapiError(-1, 0, "", std::string("HTTP ") + verb + " " + path + " connection failed: " + httplib::to_string(res.error()));
    }
    if (res->status < 200 || res->status >= 300) {
        int isapi_code = 0;
        if (auto c = extract_tag(res->body, "statusCode")) {
            try { isapi_code = std::stoi(*c); } catch (...) {}
        }
        throw IsapiError(res->status, isapi_code, res->body, std::string("HTTP ") + verb + " " + path + " failed with status " + std::to_string(res->status));
    }
    return res->body;
}

}