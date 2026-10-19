#include <string>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "parser.hpp"

// 辅助：按小端从 data[pos] 读取 uint8（带 bounds 检查）
static bool read_uint8(const std::vector<uint8_t>& v, size_t pos, uint8_t& out) {
    if (pos + 1 > v.size()) return false;
    out = v[pos];
    return true;
}

Parser::Parser() {
    const std::string log_dir = "/home/heyn/patrol_robot_h1/logs/monitor_center";
    const std::string log_path = log_dir + "/control.log";

    clear_old_logs(log_dir, 3);
	create_log(log_path);

    publisher.bind("tcp://*:6060");
}

Parser::~Parser() {
    if (logger) logger->flush();
}

void Parser::clear_old_logs(const std::string& log_dir, int max_day) {
    namespace fs = std::filesystem;
    
    if (!fs::exists(log_dir)) return;
    
    // 关键：使用 file_time_type 的时钟，而不是 system_clock
    auto now = fs::file_time_type::clock::now();
    std::regex log_pattern(R"(control_\d{4}-\d{2}-\d{2}\.log)");
    
    for (const auto& entry : fs::directory_iterator(log_dir)) {
        if (!entry.is_regular_file()) continue;
        
        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, log_pattern)) continue;
        
        auto file_time = fs::last_write_time(entry);
        // 直接相减，无需 clock_cast
        auto file_age = now - file_time;
        auto hours_old = std::chrono::duration_cast<std::chrono::hours>(file_age).count();
        int days_old = static_cast<int>(hours_old / 24);
        
        if (days_old > max_day) {
            SPDLOG_INFO("删除过期日志: {} ({}天前)", filename, days_old);
            std::error_code ec;
            fs::remove(entry.path(), ec);
            if (ec) {
                SPDLOG_WARN("删除失败: {} - {}", filename, ec.message());
            }
        }
    }
}

void Parser::create_log(const std::string& log_path) {
	auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
		log_path,
		0, // 轮转时间：小时 (0 = 午夜)
		0, // 轮转时间：分钟
		false, // 不截断现有文件
		5 // 最大保留文件数
	);
	logger = std::make_shared<spdlog::logger>("control", sink);
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
	logger->flush_on(spdlog::level::info);
}

// 解析电池
void Parser::parse_battery_info(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(BatteryInfo), "BatteryInfo")) return;
    BatteryInfo battery_info;
    std::memcpy(&battery_info, data.data(), sizeof(battery_info));

    json content = {
        {"battery_percentage", int(battery_info.SOC)},
        {"battery_percentage_text", std::to_string(int(battery_info.SOC)) + "%"},
        {"battery_temperature", int(battery_info.Temp)},
    };

    std::string message = content.dump();
    publish("power_info", message);
    logger->info("电池信息 {}", message);
}

// 解析充电状态
void Parser::parse_charging_status(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(ChargeManageStatueDef), "ChargeManageStatueDef")) return;
    ChargeManageStatueDef charge_status;
    std::memcpy(&charge_status, data.data(), sizeof(charge_status));

    json content = {
        {"is_auto_charge", charge_status.StaFlag.flagAutoCharge == 1},
        {"is_manual_charge", charge_status.StaFlag.flagManualCharge == 1},
        {"is_charge_pile_error", charge_status.StaFlag.flagChargePileErr == 1},
        {"is_charge_error", charge_status.StaFlag.flagChargeErr == 1},
    };

    std::string message = content.dump();
    publish("charging_info", message);
    logger->info("充电信息 {}", message);
}

// 解析系统状态
void Parser::parse_system_status(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(SystemStatus), "SystemStatus")) return;
    SystemStatus system_status;
    std::memcpy(&system_status, data.data(), sizeof(system_status));

    json content = {
        {"is_stop", system_status.StaFlag.flagEmergencyButtonEnter == 1},
        {"is_sos", system_status.StaFlag.flagSOSButtonEnter == 1},
        {"is_low_power", system_status.StaFlag.lowPowerStatue == 1},
        {"is_run", system_status.StaFlag.DisableChargeDetect == 1}, // 是否运动中
        {"anti_collision_strip", int(system_status.StaFlag.flagAnticollisionBarStatue)}, // 防撞条状态
    };

    std::string message = content.dump();
    publish("system_status", message);
    logger->info("系统状态 {}", message);
}

// 解析温湿度
void Parser::parse_temperature_humidity(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(TemperatureAndHumidity), "TemperatureAndHumidity")) return;
    TemperatureAndHumidity th;
    std::memcpy(&th, data.data(), sizeof(th));

    double temperature = static_cast<double>(th.temperature) / 10.0;
    double humidity = static_cast<double>(th.humidity) / 10.0;

    json content = {
        {"temperature", temperature},
        {"temperature_text", fmt::format("{:.1f}°C", temperature)},
        {"humidity", humidity},
        {"humidity_text", fmt::format("{:.1f}%", humidity)},
        {"pm1_0", th.PM1_0},
        {"pm1_0_text", std::to_string(th.PM1_0) + "ug/m3"},
        {"pm2_5", th.PM2_5},
        {"pm2_5_text", std::to_string(th.PM2_5) + "ug/m3"},
        {"pm10", th.PM10},
        {"pm10_text", std::to_string(th.PM10) + "ug/m3"},
    };

    std::string message = content.dump();
    publish("temperature_humidity_info", message);
    logger->info("温湿度信息 {}", message);
}

// 解析总里程 / 运行周期
void Parser::parse_total_runtime(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(LifecycleInfoDef), "LifecycleInfoDef")) return;
    LifecycleInfoDef info;
    std::memcpy(&info, data.data(), sizeof(info));

    double current_mileage_km = static_cast<double>(info.nowMilleage) / 1000.0;
    double total_mileage_km = static_cast<double>(info.totalMilleage) / 1000.0;

    json content = {
        {"current_mileage_m", info.nowMilleage},
        {"current_mileage_m_text", std::to_string(info.nowMilleage) + "m"},
        {"current_mileage_km", current_mileage_km},
        {"current_mileage_km_text", fmt::format("{:.3f}km", current_mileage_km)},
        {"total_mileage_m", info.totalMilleage},
        {"total_mileage_m_text", std::to_string(info.totalMilleage) + "m"},
        {"total_mileage_km", total_mileage_km},
        {"total_mileage_km_text", fmt::format("{:.3f}km", total_mileage_km)},
        {"current_run_time", info.nowRunTime},
        {"total_run_time", info.totalRunTime},
        {"powered_on_times", info.bootTimes},
        {"charged_count", info.AutoChargeNum},
    };

    std::string message = content.dump();
    publish("lifecycle_info", message);
    logger->info("里程时长信息 {}", message);
}

// 解析按钮状态
void Parser::parse_button_status(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    uint8_t btn = 0;
    if (!read_uint8(data, 0, btn)) {
        SPDLOG_WARN("0x1204 按钮帧长度不足");
        return;
    }
    bool PowerButton = (btn & (1u << 0)) != 0;
    bool EmergencyButton = (btn & (1u << 1)) != 0;
    bool SOSButton = (btn & (1u << 2)) != 0;

    json content = {
        {"stop_button", EmergencyButton},
        {"power_button", PowerButton},
        {"sos_button", SOSButton},
    };

    std::string message = content.dump();
    publish("button_info", message);
    logger->info("按钮信息 {}", message);
}

// 解析 GPS（字段为缩放整数，需还原）
void Parser::parse_gps(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    if (!check_len(data, sizeof(GpsInfo), "GpsInfo")) return;
    GpsInfo gps_info;
    std::memcpy(&gps_info, data.data(), sizeof(gps_info));

    // 注意：经纬度/海拔/速度/方向均为缩放整数，前端负责还原
    // 经纬度 *1000000，海拔 *100，速度 *1000，方向 *100
    json content = {
        {"valid", gps_info.Valid},
        {"ew", gps_info.EW},
        {"sn", gps_info.SN},
        {"longitude", gps_info.Longitude},
        {"latitude", gps_info.Latitude},
        {"asl", gps_info.ASL},
        {"speed", gps_info.Speed},
        {"direction", gps_info.Direction},
    };

    std::string message = content.dump();
    publish("gps_info", message);
    logger->info("GPS信息 {}", message);
}

// 解析版本(48字节预留)
void Parser::parse_version(const std::vector<uint8_t>& data) {
    using namespace nlohmann;

    std::string version;
    auto nul = std::find(data.begin(), data.end(), 0); // 可能含 '\0' 终止符或不足 48 字节
    version.assign(data.begin(), nul);

    json content = {
        {"version", version}
    };

    std::string message = content.dump();
    publish("power_version", message);
    logger->info("版本信息 {}", message);

    version_handler.save(version);
}

bool Parser::check_len(const std::vector<uint8_t>& data, size_t need, const char* type_name) {
    if (data.size() < need) {
        SPDLOG_WARN("帧长度不足: {} < sizeof({})={}", data.size(), type_name, need);
        return false;
    }
    return true;
}

// 广播
void Parser::publish(const std::string& topic, const std::string& message) {
    zmq::message_t topic_msg(topic.begin(), topic.end());
    zmq::message_t data_msg(message.begin(), message.end());
    publisher.send(topic_msg, zmq::send_flags::sndmore);
    publisher.send(data_msg, zmq::send_flags::none);
}