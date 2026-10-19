#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "ptz_controller.hpp"

using u8 = uint8_t;
using Frame = std::vector<u8>;
using namespace std::chrono_literals;

PTZController::PTZController() {
    fd = open_serial();
}

PTZController::~PTZController() {
    close_serial();
}

// ======================= UART相关 ============================
int PTZController::open_serial() {
    speed_t speed = B9600;
    struct termios tty;

    int fd = open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        SPDLOG_ERROR("打开失败");
        return -1;
    }
    if (tcgetattr(fd, &tty) != 0) {
        SPDLOG_ERROR("tcgetattr");
        close(fd);
        return -1;
    }
    cfmakeraw(&tty);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag &= ~PARENB; // no parity
    tty.c_cflag &= ~CSTOPB; // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8; // 8 bits
    tty.c_cflag |= CREAD | CLOCAL; // turn on READ & ignore ctrl lines
    // Non-canonical read: return either when 7 bytes available or timeout
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5; // 0.5s read timeout (per 0.1s units)

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

void PTZController::close_serial() {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool PTZController::send_frame(int fd, const Frame &f) {
    if (f.size() != 7) return false;
    ssize_t w = write(fd, f.data(), f.size());
    if (w < 0) {
        perror("write");
        return false;
    }
    // flush to make sure bytes go out
    tcdrain(fd);
    return (size_t)w == f.size();
}

bool PTZController::read_frame(int fd, Frame &out, int timeout_seconds) {
    // read exactly one 7-byte Pelco-D frame (wait up to timeout_seconds)
    out.clear();
    fd_set rfds;
    timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;

    size_t idx = 0;
    const size_t FRAME_LEN = 7;
    out.resize(FRAME_LEN);

    // We search for a 0xFF start, then read remaining 6 bytes
    // Loop until timeout
    int rv;
    while (true) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        rv = select(fd+1, &rfds, NULL, NULL, &tv);
        if (rv == -1) {
            perror("select");
            return false;
        } else if (rv == 0) {
            // timeout
            return false;
        } else {
            // data available
            u8 b;
            ssize_t r = read(fd, &b, 1);
            if (r <= 0) {
                if (r < 0) perror("read");
                return false;
            }
            if (idx == 0) {
                if (b == 0xFF) {
                    out[0] = b;
                    idx = 1;
                    // now read remaining 6 bytes with small loop
                    ssize_t rem = FRAME_LEN - idx;
                    ssize_t got = 0;
                    while (got < rem) {
                        ssize_t rr = read(fd, &out[idx+got], rem-got);
                        if (rr > 0) got += rr;
                        else if (rr == 0) {
                            // wait a bit
                            usleep(10000);
                        } else {
                            perror("read");
                            return false;
                        }
                    }
                    // validate checksum
                    u8 cs = checksum5(out);
                    if (cs != out[6]) {
                        // invalid, continue search
                        idx = 0;
                        // shift window: keep searching for next 0xFF
                        continue;
                    }
                    return true;
                } else {
                    // discard and continue
                    continue;
                }
            }
        }
    }
    return false;
}

Frame PTZController::build_base_frame(u8 addr, u8 cmd1, u8 cmd2, u8 data1, u8 data2) {
    Frame f(7);
    f[0] = 0xFF;
    f[1] = addr;
    f[2] = cmd1;
    f[3] = cmd2;
    f[4] = data1;
    f[5] = data2;
    f[6] = checksum5(f);
    return f;
}

u8 PTZController::checksum5(const Frame &f) {
    // checksum = sum(bytes 2..6) & 0xFF
    // our Frame layout: full 7 bytes (index 0..6), index 0 is 0xFF
    int sum = 0;
    for (int i = 1; i <= 5; ++i) sum += f[i];
    return (u8)(sum & 0xFF);
}

// ======================= 云台相关 ============================
bool PTZController::to_up() {
    Frame f = build_base_frame(addr, 0x00, 0x08, 0x00, 0x1E);
    auto result = send_frame(fd, f);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop();
    return result;
}

bool PTZController::to_down() {
    Frame f = build_base_frame(addr, 0x00, 0x10, 0x00, 0x1E);
    auto result = send_frame(fd, f);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop();
    return result;
}

bool PTZController::to_left() {
    Frame f = build_base_frame(addr, 0x00, 0x04, 0x20, 0x00);
    auto result = send_frame(fd, f);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop();
    return result;
}

bool PTZController::to_right() {
    Frame f = build_base_frame(addr, 0x00, 0x02, 0x20, 0x00);
    auto result = send_frame(fd, f);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    stop();
    return result;
}

bool PTZController::stop() {
    Frame f = build_base_frame(addr, 0x00, 0x00, 0x00, 0x00); // FF 01 00 00 00 00 01
    return send_frame(fd, f);
}

void PTZController::reset_ptz() {
    set_pan_abs(359.99);
    set_tilt_abs(0.00);
}

// ======================= 调焦相关 ============================
void PTZController::zoom_in() {
    cam.zoom_in(60);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cam.stop();
}

void PTZController::zoom_out() {
    cam.zoom_out(60);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    cam.stop();
}

void PTZController::zoom_to() {
    cam.set_zoom_x(5.0);
}

void PTZController::zoom_reset() {
    cam.set_zoom_x(1.0);
}

// ======================= 雨刷相关 ============================
void PTZController::turn_on_wiper(WiperMode mode) {
    Frame f = build_base_frame(addr, 0x00, 0x09, 0x00, 0x01);
    send_frame(fd, f);

    if (mode == WiperMode::CONTINUOUS_BRUSH) {
        try {
            sql::Connection* con = DBConnection::get_instance().get_connection();
            std::string sql = "UPDATE settings_basic SET config_value = 'on' WHERE config_key = 'continuous_wiper'";
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            stmt->executeUpdate(sql);
            SPDLOG_INFO("连刷已开启，数据库已更新");
        } catch (const std::exception& e) {
            SPDLOG_ERROR("连刷异常: {}", e.what());
        }
    }
}

void PTZController::turn_off_wiper(WiperMode mode) {
    Frame f = build_base_frame(addr, 0x00, 0x0B, 0x00, 0x01);
    send_frame(fd, f);

    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE settings_basic SET config_value = 'off' WHERE config_key = 'continuous_wiper'";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        stmt->executeUpdate(sql);
        SPDLOG_INFO("雨刷已关闭，数据库已更新");
    } catch (const std::exception& e) {
        SPDLOG_ERROR("关雨刷异常: {}", e.what());
    }
}

// ======================= 设置角度 ============================
bool PTZController::set_pan_abs(double angle_deg) {
    // Normalize to 0..359.99
    while (angle_deg < 0) angle_deg += 360.0;
    while (angle_deg >= 360.0) angle_deg -= 360.0;
    uint32_t val = (uint32_t) (angle_deg * 100.0 + 0.5); // integer hundredths
    u8 ph = (val >> 8) & 0xFF;
    u8 pl = val & 0xFF;
    // command2 = 0x4B per doc for set pan position
    Frame f = build_base_frame(addr, 0x00, 0x4B, ph, pl);
    return send_frame(fd, f);
}

bool PTZController::set_tilt_abs(double angle_deg) {
    // 直接按设备协议编码
    uint32_t val = (uint32_t)(angle_deg * 100.0 + 0.5);
    if (val > 35999) val = 35999;
    
    u8 th = (val >> 8) & 0xFF;
    u8 tl = val & 0xFF;
    Frame f = build_base_frame(addr, 0x00, 0x4D, th, tl);
    return send_frame(fd, f);
}

// ======================= 查询角度 ============================
bool PTZController::query_pan(double &out_angle) {
    Frame q = build_base_frame(addr, 0x00, 0x51, 0x00, 0x00);
    if (!send_frame(fd, q)) return false;
    Frame r;
    if (!read_frame(fd, r, 1)) return false;
    // expect r[3] == 0x59
    if (r.size() == 7 && r[3] == 0x59) {
        uint16_t pval = (r[4] << 8) | r[5];
        out_angle = pval / 100.0;
        return true;
    }
    return false;
}

bool PTZController::query_tilt(double &out_angle) {
    Frame q = build_base_frame(addr, 0x00, 0x53, 0x00, 0x00);
    if (!send_frame(fd, q)) {
        return false;
    }

    Frame r;
    if (!read_frame(fd, r, 1)) {
        return false;
    }

    if (r.size() == 7 && r[3] == 0x5B) {
        uint16_t tval = (r[4] << 8) | r[5];
        SPDLOG_INFO("tval: {}", tval);
        out_angle = tval / 100.0;
        return true;
    }
    return false;
}

// ======================= 执行预置位 ===========================
void PTZController::execute_preset_position(const std::string& code) {
    try {
        auto* conn = DBConnection::get_instance().get_connection();

        std::string sql = "SELECT horizontal_angle, vertical_angle FROM preset_position WHERE preset_position_code = ?";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setInt(1, std::stoi(code));
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

        double horizontal_angle = 0.00;
        double vertical_angle = 0.00;

        if (res->next()) {
            horizontal_angle = res->getDouble("horizontal_angle");
            vertical_angle = res->getDouble("vertical_angle");
            SPDLOG_INFO("加载预置位 {}: horizontal={}, vertical={}", code, horizontal_angle, vertical_angle);
        } else {
            SPDLOG_ERROR("未找到预置位: {}", code);
            return;
        }

        set_pan_abs(horizontal_angle);
        set_tilt_abs(vertical_angle);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("执行预置位失败: {}", e.what());
    }
}