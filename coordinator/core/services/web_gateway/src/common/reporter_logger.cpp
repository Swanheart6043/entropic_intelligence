#include "zmq_log_sink.hpp"
#include "reporter_logger.hpp"
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace reporter_logger {
void ReporterLogger::init() {
    if (spdlog::get("reporter_logger")) {
        return;
    }

    std::vector<spdlog::sink_ptr> sinks;

    auto zmq_sink = std::make_shared<patrol_robot::ZmqLogSink>("reporter_logger");
    sinks.push_back(zmq_sink);

    auto file_sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(
        "/home/heyn/patrol_robot_h1/logs/web_gateway/reporter.log",
        0,
        0,
        false,
        5
    );
    file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    sinks.push_back(file_sink);

    auto logger = std::make_shared<spdlog::logger>("reporter_logger", sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::err);

    spdlog::register_logger(logger);
}

std::shared_ptr<spdlog::logger> ReporterLogger::get() {
    auto logger = spdlog::get("reporter_logger");
    if (!logger) {
        // 如果未初始化，返回默认 logger（避免崩溃）
        return spdlog::default_logger();
    }
    return logger;
}
}