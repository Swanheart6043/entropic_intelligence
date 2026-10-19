#include <filesystem>
#include <regex>
#include <iconv.h>
#include <errno.h>
#include <cstring>
#include "mcu_log_service.hpp"

McuLogService::McuLogService(std::shared_ptr<boost::asio::io_context> io_ctx) : udp(io_ctx) {
	const std::string log_dir = "/home/heyn/patrol_robot_h1/logs/monitor_center";
    const std::string log_path = log_dir + "/mcu.log";

	clear_old_logs(log_dir, 7);
	create_log(log_path);
	
	if (!udp.init("0.0.0.0", 0, 6003)) {
		throw std::runtime_error("主控日志服务udp初始化失败! ");
	}
	udp.setMessageCallback([this](const std::string& message, const udp::endpoint&) {
		save_message(message);
	});
}

McuLogService::~McuLogService() {
	if (logger) logger->flush();
}

void McuLogService::start() {
	udp.start();
}

void McuLogService::stop() { 
	udp.stop();
}

void McuLogService::clear_old_logs(const std::string& log_dir, int max_day) {
	namespace fs = std::filesystem;
    
    if (!fs::exists(log_dir)) return;
    
    // 关键：使用 file_time_type 的时钟，而不是 system_clock
    auto now = fs::file_time_type::clock::now();
    std::regex log_pattern(R"(mcu_\d{4}-\d{2}-\d{2}\.log)");
    
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

void McuLogService::create_log(const std::string& log_path) {
	auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
		log_path,
		0, // 轮转时间：小时 (0 = 午夜)
		0, // 轮转时间：分钟
		false, // 不截断现有文件
		14 // 最大保留文件数
	);
	logger = std::make_shared<spdlog::logger>("mcu", sink);
	logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
	logger->flush_on(spdlog::level::info);
}

void McuLogService::save_message(const std::string& message) {
	if (message.empty()) return;

    std::string line = gbk_to_utf8(message);
    
	// 去掉末尾可能的 \r\n 免得产生空行
	while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
		line.pop_back();
	}
	if (!line.empty()) {
		logger->info("{}", line);
	}
}

std::string McuLogService::gbk_to_utf8(const std::string& gbk) {
    if (gbk.empty()) return gbk;
 
    iconv_t cd = iconv_open("UTF-8", "GBK");
    if (cd == reinterpret_cast<iconv_t>(-1)) {
        return gbk;
    }

    // UTF-8 最多 4 字节/字符, 留 4 倍空间足够
    std::string out(gbk.size() * 4, '\0');
 
    // iconv 输入输出指针会被修改, 需要用临时变量
    size_t in_left = gbk.size();
    size_t out_left = out.size();
    char* in_ptr = const_cast<char*>(gbk.data());
    char* out_ptr = &out[0];
    char* out_start = out_ptr;
 
    // IGNORE 能让非法字节被跳过, 这里不用, 直接在失败时返回原文
    size_t rc = iconv(cd, &in_ptr, &in_left, &out_ptr, &out_left);
    iconv_close(cd);
 
    if (rc == static_cast<size_t>(-1)) {
        return gbk; // 转换失败, 原文留底
    }
 
    out.resize(out_ptr - out_start);
    return out;
}