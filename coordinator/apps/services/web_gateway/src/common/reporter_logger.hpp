#pragma once
#include <spdlog/spdlog.h>
#include <memory>

namespace reporter_logger {
class ReporterLogger {
public:
    ReporterLogger() = delete;
    ReporterLogger(const ReporterLogger&) = delete;
    ReporterLogger& operator=(const ReporterLogger&) = delete;
    static void init();
    static std::shared_ptr<spdlog::logger> get();
};

#define REPORTER_LOG_TRACE(...) reporter_logger::ReporterLogger::get()->trace(__VA_ARGS__)
#define REPORTER_LOG_DEBUG(...) reporter_logger::ReporterLogger::get()->debug(__VA_ARGS__)
#define REPORTER_LOG_INFO(...) reporter_logger::ReporterLogger::get()->info(__VA_ARGS__)
#define REPORTER_LOG_WARN(...) reporter_logger::ReporterLogger::get()->warn(__VA_ARGS__)
#define REPORTER_LOG_ERROR(...) reporter_logger::ReporterLogger::get()->error(__VA_ARGS__)
#define REPORTER_LOG_CRITICAL(...) reporter_logger::ReporterLogger::get()->critical(__VA_ARGS__)
}
