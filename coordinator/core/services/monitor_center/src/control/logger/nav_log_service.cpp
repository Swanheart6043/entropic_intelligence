#include <cstdint>
#include <string>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include "nav_log_service.hpp"

namespace {
    uint16_t read_u16_le(const std::string& data, std::size_t offset) {
        const auto low = static_cast<uint8_t>(static_cast<unsigned char>(data[offset]));
        const auto high = static_cast<uint8_t>(static_cast<unsigned char>(data[offset + 1]));
        return static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
    }
    std::string bytes_to_hex(const std::string& data) {
        static constexpr char hex_chars[] = "0123456789ABCDEF";
        std::string hex;
        hex.reserve(data.size() * 3);
        
        for (std::size_t i = 0; i < data.size(); ++i) {
            const auto value = static_cast<unsigned char>(data[i]);
            if (i > 0) hex.push_back(' ');
            hex.push_back(hex_chars[(value >> 4) & 0x0F]);
            hex.push_back(hex_chars[value & 0x0F]);
        }
        return hex;
    }
}

NavLogService::NavLogService(std::shared_ptr<boost::asio::io_context> io_ctx) : udp(io_ctx) {
	const std::string log_dir = "/home/heyn/patrol_robot_h1/logs/monitor_center";
    const std::string log_path = log_dir + "/nav.log";

    clear_old_logs(log_dir, 5);
	create_log(log_path);
	
	if (!udp.init("0.0.0.0", 0, 6002)) {
		throw std::runtime_error("主控与导航日志udp初始化失败! ");
	}
	udp.setMessageCallback([this](const std::string& message, const udp::endpoint&) {
		save_message(message);
	});
}

NavLogService::~NavLogService() {
	if (logger) logger->flush();
}

void NavLogService::start() {
	udp.start();
}

void NavLogService::stop() { 
	udp.stop();
}

void NavLogService::clear_old_logs(const std::string& log_dir, int max_day) {
    namespace fs = std::filesystem;
    
    if (!fs::exists(log_dir)) return;
    
    // 关键：使用 file_time_type 的时钟，而不是 system_clock
    auto now = fs::file_time_type::clock::now();
    std::regex log_pattern(R"(nav_\d{4}-\d{2}-\d{2}\.log)");
    
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

void NavLogService::create_log(const std::string& log_path) {
	auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
		log_path,
		0, // 轮转时间：小时 (0 = 午夜)
		0, // 轮转时间：分钟
		false, // 不截断现有文件
		5 // 最大保留文件数
	);
	logger = std::make_shared<spdlog::logger>("nav", sink);
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
	logger->flush_on(spdlog::level::info);
}

void NavLogService::save_message(const std::string& message) {
    if (message.empty()) {
        return;
    }
    const std::string hex = bytes_to_hex(message);
    constexpr std::size_t kMinFrameSize = 8;
    if (message.size() < kMinFrameSize) {
        logger->warn("收到不完整UDP帧: actual_len={}, raw={}", message.size(), hex);
        return;
    }
    
    const auto frame_comp = static_cast<uint8_t>(static_cast<unsigned char>(message[0]));
    const auto ident = static_cast<uint8_t>(static_cast<unsigned char>(message[1]));
    const uint16_t frame_len = read_u16_le(message, 2);
    const uint16_t funid = read_u16_le(message, 4);

    logger->info(
        "funid=0x{:04X}, protocol_len={}, actual_len={}, "
        "frame_comp=0x{:02X}, ident=0x{:02X}, raw={}",
        funid,
        frame_len,
        message.size(),
        frame_comp,
        ident,
        hex
    );
}