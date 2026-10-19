#include <iostream>
#include <chrono>
#include <filesystem>
#include <regex>
#include <vector>
#include <memory>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "crash_handler.hpp"
#include "zmq_log_sink.hpp"
#include "bootstrap.hpp"

namespace {
    constexpr const char* kBaseDir = "/home/heyn/patrol_robot_h1";

    // 清理过期日志（init 的私有实现细节，不对外暴露）
    void cleanup_old_logs(const std::string& log_dir, const std::string& app_name, int max_days) {
        namespace fs = std::filesystem;
        if (!fs::exists(log_dir)) return;

        auto now = fs::file_time_type::clock::now();
        std::regex log_pattern(app_name + R"(_\d{4}-\d{2}-\d{2}\.log)");

        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (!entry.is_regular_file()) continue;

            std::string filename = entry.path().filename().string();
            if (!std::regex_match(filename, log_pattern)) continue;

            auto file_age = now - fs::last_write_time(entry);
            auto hours_old = std::chrono::duration_cast<std::chrono::hours>(file_age).count();
            int days_old = static_cast<int>(hours_old / 24);

            if (days_old > max_days) {
                SPDLOG_INFO("删除过期日志: {} ({}天前)", filename, days_old);
                std::error_code ec;
                fs::remove(entry.path(), ec);
                if (ec) SPDLOG_WARN("删除失败: {} - {}", filename, ec.message());
            }
        }
    }
}

namespace patrol_robot {
    void init_crash_handler(const std::string& app_name) {
        const std::string crash_dir = std::string(kBaseDir) + "/crashes/reports";
        crash::init({
            .crash_dir = crash_dir,
            .app_name = app_name,
            .enable_core_dump = true,
            .max_core_size = 200 * 1024 * 1024 // 200MB
        });
        crash::cleanup_old_crashes(crash_dir, 7); // 保留最近 7 个
    }

    void init_logging(const std::string& app_name, int max_days) {
        try {
            const std::string log_dir = std::string(kBaseDir) + "/logs/" + app_name;
            const std::string log_path = log_dir + "/" + app_name + ".log";
            cleanup_old_logs(log_dir, app_name, max_days);

            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(spdlog::level::debug);

            auto daily_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
                log_path, 
                0, 
                0, 
                false, 
                max_days
            );
            daily_sink->set_level(spdlog::level::info);

            // 磁盘熔断：warn 以上单独限流，单文件 200MB × 5 个，硬上限 1GB, 防止异常 busy loop 刷日志撑爆磁盘（daily sink 单天文件无大小上限）
            auto rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_dir + "/" + app_name + "_rotating.log",
                200 * 1024 * 1024,
                5
            );
            rotating_sink->set_level(spdlog::level::warn);

            std::vector<spdlog::sink_ptr> sinks {console_sink, daily_sink, rotating_sink};
            auto logger = std::make_shared<spdlog::logger>(app_name, sinks.begin(), sinks.end());
            logger->sinks().push_back(std::make_shared<ZmqLogSink>(app_name));
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
            logger->set_level(spdlog::level::debug);
            spdlog::set_default_logger(logger);
            spdlog::flush_every(std::chrono::seconds(1));

            SPDLOG_INFO("{} 日志初始化完成，按天轮转保留{}天, 熔断上限1GB", app_name, max_days);
        } catch (const spdlog::spdlog_ex& ex) {
            std::cout << app_name << "日志初始化失败: " << ex.what() << std::endl;
        }
    }
}