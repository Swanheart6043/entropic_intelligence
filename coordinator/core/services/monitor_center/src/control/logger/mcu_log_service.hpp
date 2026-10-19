#pragma once
#include <memory>
#include <stdexcept>
#include <string>
#include <boost/asio.hpp>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/spdlog.h>
#include "../common/udp_client.hpp"

class McuLogService {
public:
    McuLogService(std::shared_ptr<boost::asio::io_context> io_ctx);
    ~McuLogService();
    void start();
    void stop();

private:
    std::shared_ptr<spdlog::logger> logger;
    UdpClient udp;

    void clear_old_logs(const std::string& dir, int max_day);
    void create_log(const std::string& path);
    void save_message(const std::string& message);
    std::string gbk_to_utf8(const std::string& gbk);
};