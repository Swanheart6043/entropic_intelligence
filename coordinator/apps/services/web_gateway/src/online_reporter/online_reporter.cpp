#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "common/reporter_logger.hpp"
#include "online_reporter.hpp"

OnlineReporter::OnlineReporter(WebSocketClient& websocket_connect) : websocket_connect_(websocket_connect) {
    
}

OnlineReporter::~OnlineReporter() {
    stop();
}

void OnlineReporter::start() {
    if (report_thread_.joinable()) {
        auto logger = spdlog::get("web_gateway");
        SPDLOG_LOGGER_WARN(logger, "上线报告已在运行中");
        return;
    }
    
    should_stop_ = false;
    is_online_ = false;
    retry_count_ = 0;
    report_thread_ = std::thread(&OnlineReporter::report_loop, this);
}

void OnlineReporter::stop() {
    should_stop_ = true;
    cv_.notify_all();
    
    if (report_thread_.joinable()) {
        report_thread_.join();
    }
}

void OnlineReporter::report_loop() {
    auto logger = spdlog::get("web_gateway");
    int wait_time = INITIAL_RETRY_INTERVAL;
    while (!should_stop_) {
        // 阶段1：上线（带指数退避）
        while (!should_stop_ && !is_online_) {
            retry_count_++;
            SPDLOG_LOGGER_INFO(logger, "尝试上线报告 [第{}次]", retry_count_.load());
            
            if (try_report_once()) {
                is_online_ = true;
                wait_time = INITIAL_RETRY_INTERVAL;  // 重置等待时间
                SPDLOG_LOGGER_INFO(logger, "机器人上线成功！总共尝试{}次", retry_count_.load());
                break;
            }
            
            SPDLOG_LOGGER_WARN(logger, "上线失败，{}秒后重试...", wait_time);
            
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(wait_time), [this]{
                return should_stop_.load();
            });
            wait_time = std::min(wait_time * 2, MAX_RETRY_INTERVAL);
        }

        // 阶段2：心跳维持
        while (!should_stop_ && is_online_) {
            send_heartbeat();
            
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(31), [this] {
                return should_stop_.load() || !is_online_.load();
            });
        }

        if (!is_online_ && !should_stop_) {
            SPDLOG_LOGGER_WARN(logger, "连接断开，准备重新上线...");
        }
    }
    
    if (!is_online_) {
        SPDLOG_LOGGER_ERROR(logger, "上线流程结束");
    }
}

bool OnlineReporter::try_report_once() {
    auto logger = spdlog::get("web_gateway");

    try {
        // 检查网络连通性
        if (!check_network_connectivity()) {
            SPDLOG_LOGGER_WARN(logger, "网络不可达，跳过本次尝试");
            return false;
        }
        // 上线请求
        bool http_success = send_online();
        return http_success;
        
    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "上线上报异常: {}", e.what());
        return false;
    }
}

bool OnlineReporter::check_network_connectivity() {
    // ping网关
    std::string cmd = "ping -c 1 -W 2 10.10.10.1 > /dev/null 2>&1";
    if (system(cmd.c_str()) == 0) {
        return true;
    }
    SPDLOG_INFO("网络不可达");
    return false;
}

bool OnlineReporter::send_online() {
    auto logger = spdlog::get("web_gateway");

    try {
        httplib::Client http(config->get_platform_http_server_url());
        http.set_connection_timeout(5, 0);
        http.set_read_timeout(5, 0);
        http.set_write_timeout(5, 0);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        http.enable_server_certificate_verification(config->get_cloud_tls_verify());
        if (!config->get_cloud_ca_cert_path().empty()) {
            http.set_ca_cert_path(config->get_cloud_ca_cert_path());
        }
#endif
        
        httplib::Headers headers;
        httplib::Params params{
            {"code", config->get_sn()}, 
            {"codeKey", config->get_sn()}
        };
        auto response = http.Get(
            config->get_platform_http_api_path("/device/updateDeciveDetect"),
            params, 
            headers
        );
        if (!response) {
            auto err = response.error();
            SPDLOG_LOGGER_WARN(logger, "  HTTP请求失败: {}", httplib::to_string(err));
            return false;
        }
        if (response->status == 200) {
            SPDLOG_LOGGER_INFO(logger, "HTTP上线成功: {}", response->body);
            return true;
        } else {
            SPDLOG_LOGGER_WARN(logger, "HTTP请求返回[{}]: {}", response->status, response->body);
            return false;
        }

    } catch (const std::exception& e) {
        SPDLOG_LOGGER_ERROR(logger, "HTTP上线异常: {}", e.what());
        return false;
    }
}

void OnlineReporter::send_heartbeat() {    
    auto logger = spdlog::get("web_gateway");
    
    nlohmann::json heartbeat = {
        {"Header", {
            {"CmdType", "Heartbeat"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)} 
        }}
    };
    
    if (!websocket_connect_.send_message(heartbeat.dump())) {
        // 发送失败，标记离线，触发重连
        SPDLOG_LOGGER_WARN(logger, "心跳发送失败，标记离线");
        is_online_ = false;
        return;
    }

    REPORTER_LOG_INFO("心跳: {}", heartbeat.dump());
}
