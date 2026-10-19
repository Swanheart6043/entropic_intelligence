#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "navigator.hpp"

Navigator::Navigator() {
    socket_to_audio.connect("ipc://@edge_gateway_push_pull");
    socket_to_web.connect("tcp://127.0.0.1:2007");

    http = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
    websocket = std::make_unique<WebSocketClient>("10.10.10.10:8089/gs-robot/notice/status");
    websocket->set_message_callback([this](const std::string& msg) {listening(msg);});
    websocket->start_client();
}

Navigator::~Navigator() {

}

Navigator& Navigator::instance() {
    static Navigator instance;
    return instance;
}

NavigationResult Navigator::nav_to_normal_point(const std::string& map_name, const std::string& point_name) {
    try {
        // 必须，导航接口请求太快 < 1秒时会不通
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // 重置状态
        {
            std::lock_guard<std::mutex> lock(nav_mutex);
            runtime_status.reset();
            final_result.reset();
            cancel.store(false);
            paused.store(false);
            navigating.store(true);
            lost_resent = false;
            if (!last_navigation_was_point_unreachable) {
                consecutive_point_unreachable_count = 0;
                point_unreachable_streak_reported = false;
            }
            last_navigation_was_point_unreachable = false;

            PendingCommand cmd;
            cmd.is_custom = false;
            cmd.map_name = map_name;
            cmd.point_name = point_name;
            pending_command = std::move(cmd);
        }
        
        // 通知导航模块
        auto response = http->Get(
            base_url + "/cmd/position/navigate?map_name=" + 
            map_name + "&position_name=" + point_name
        );
        // 判断结果
        if (!response || response->status != 200) {
            SPDLOG_ERROR("导航请求失败");
            navigating.store(false);
            return NavigationResult::FAILED;
        }
        SPDLOG_INFO("---------------------------------------");
        SPDLOG_INFO("导航开始: {} ({})", map_name, point_name);
        
        // 阻塞等待导航到目标地点
        auto result = waiting_for_navigation();

        // 善后
        SPDLOG_INFO("导航结束");
        SPDLOG_INFO("---------------------------------------");
        navigating.store(false);
        return result;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("导航到导航点异常: {}", e.what());
        navigating.store(false);
        return NavigationResult::FAILED;
    }
}

NavigationResult Navigator::nav_to_custom_point(const std::string& map_name, int x, int y, int angle) {
    try {
        // 必须，导航接口请求太快时 < 1秒时会不通
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 重置状态
        {
            std::lock_guard<std::mutex> lock(nav_mutex);
            runtime_status.reset();
            final_result.reset();
            cancel.store(false);
            paused.store(false);
            navigating.store(true);
            lost_resent = false;
            if (!last_navigation_was_point_unreachable) {
                consecutive_point_unreachable_count = 0;
                point_unreachable_streak_reported = false;
            }
            last_navigation_was_point_unreachable = false;

            PendingCommand cmd;
            cmd.is_custom = true;
            cmd.map_name = map_name;
            cmd.x = x;
            cmd.y = y;
            cmd.angle = angle;
            pending_command = std::move(cmd);
        }
        
        // 通知导航模块
        nlohmann::json params = {
            {"destination", {
                {"mapName", map_name},
                {"angle", angle}, 
                {"gridPosition", {{"x", x}, {"y", y}}}
            }}
        };
        auto response = http->Post(base_url + "/cmd/navigate", params.dump(), "application/json");
        // 判断结果
        if (!response || response->status != 200) {
            SPDLOG_ERROR("导航请求失败");
            navigating.store(false);
            return NavigationResult::FAILED;
        }

        SPDLOG_INFO("---------------------------------------");
        SPDLOG_INFO("导航开始: {} ({}, {}, {})", map_name.empty() ? "未命名" : map_name, x, y, angle);
        
        // 阻塞等待导航到目标地点
        NavigationResult result = waiting_for_navigation();

        // 善后
        SPDLOG_INFO("导航结束");
        SPDLOG_INFO("---------------------------------------");
        navigating.store(false);
        return result;

    } catch (const std::exception& e) {
        SPDLOG_ERROR("导航到自定义点异常: {}", e.what());
        navigating.store(false);
        return NavigationResult::FAILED;
    }
}

NavigationResult Navigator::waiting_for_navigation() {
    std::unique_lock<std::mutex> lock(nav_mutex);
    auto last_heartbeat = std::chrono::steady_clock::now();
    
    while (true) {
        // 条件变量等待通知
        nav_cv.wait_for(lock, std::chrono::seconds(1));
        // 记录时刻
        auto now = std::chrono::steady_clock::now();

        if (final_result.has_value()) {
            auto result = *final_result;
            final_result.reset();
            runtime_status.reset();
            return result;
        }
        if (paused.load()) {
            SPDLOG_INFO("导航被暂停");
            return NavigationResult::PAUSED;
        }
        if (cancel.load()) {
            SPDLOG_INFO("导航被取消");
            return NavigationResult::CANCELLED;
        }
        
        // 定位丢失后静默了一段时间 + 还没重发过 → 视为恢复，补发一次
        bool in_lost = runtime_status.has_value() && runtime_status->localization_lost_start.has_value();
        bool quiet = runtime_status.has_value() && (now - runtime_status->last_update) >= RESEND_QUIET_PERIOD;
        if (in_lost && quiet && !lost_resent) {
            auto lost_start_snapshot = runtime_status->localization_lost_start;
            lock.unlock();
            bool ok = resend_navigation();
            lock.lock();

            bool same_episode = runtime_status.has_value() && runtime_status->localization_lost_start == lost_start_snapshot;
            if (ok && same_episode) {
                lost_resent = true;
                SPDLOG_INFO("定位疑似恢复，已补发导航命令");
            }
        }

        if (runtime_status.has_value()) {
            last_heartbeat = runtime_status->last_update;
        }
        if (now - last_heartbeat > HEARTBEAT_TIMEOUT) {
            SPDLOG_ERROR("心跳超时");
            return NavigationResult::TIMEOUT;
        }
    }
}

void Navigator::listening(const std::string& message) {
    std::lock_guard<std::mutex> lock(nav_mutex);
    if (!navigating.load()) {
        SPDLOG_DEBUG("没运动下收到的消息: {}", message);
        return;
    }
    
    try {
        auto data = nlohmann::json::parse(message);
        int code = data.at("statusCode");
        std::string status_msg = data.at("statusMsg");
        auto now = std::chrono::steady_clock::now();
        
        // 导航中心跳（5秒一次）
        static auto last_log = std::chrono::steady_clock::time_point{};
        bool is_heartbeat = (code == 405 || code == 302 || code == 1006);
        if (!is_heartbeat || now - last_log > std::chrono::seconds(5)) {
            SPDLOG_INFO("导航消息: {{code:{}, msg:{}}}", code, status_msg);
            if (is_heartbeat) last_log = now;
        }

        // 判断状态变化
        switch (code) {
            case 405: {
                runtime_status = RuntimeStatus{code, status_msg, now, std::nullopt};
                lost_resent = false;
                nav_cv.notify_one();
                break;
            }
            case 302: {
                runtime_status = RuntimeStatus{code, status_msg, now, std::nullopt};
                lost_resent = false;
                nav_cv.notify_one();
                break;
            }
            case 304: {
                runtime_status = RuntimeStatus{code, status_msg, now, std::nullopt};
                lost_resent = false;
                nav_cv.notify_one();
                break;
            }
            case 407: {
                final_result = NavigationResult::SUCCESS;
                nav_cv.notify_one();
                break;
            }
            case 306: {
                final_result = NavigationResult::SUCCESS;
                nav_cv.notify_one();
                break;
            }
            case 404: {
                handle_point_unreachable();
                break;
            }
            case 408: {
                handle_point_unreachable();
                break;
            }
            case 702: {
                handle_in_virtual_wall();
                break;
            }
            case 1006: {
                handle_location_lost(code, status_msg, now);
                break;
            }
            default: {
                handle_nav_unknown_error(code, status_msg);
                break;
            }
        }
    } catch (const std::exception& e) {
        SPDLOG_ERROR("解析导航消息异常: {}", e.what());
        final_result = NavigationResult::FAILED;
        nav_cv.notify_one();
    }
}

// 同一次导航 404/408 消息只会计数一次
void Navigator::handle_point_unreachable() {
    if (final_result.has_value()) {
        SPDLOG_WARN("重复 - 无法到达的终端状态被忽略");
        return;
    }

    last_navigation_was_point_unreachable = true;
    ++consecutive_point_unreachable_count;

    const int report_threshold = point_unreachable_report_threshold;
    SPDLOG_WARN("点位不可达次数: {}/{}", consecutive_point_unreachable_count, report_threshold);

    final_result = NavigationResult::SKIPPED;
    nav_cv.notify_one();

    if (consecutive_point_unreachable_count < report_threshold) return;
    if (point_unreachable_streak_reported) {
        SPDLOG_WARN("点位无法到达的连败记录已报告；重复报告已跳过");
        return;
    }

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "50104"}}}
    };
    auto result = socket_to_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    if (!result) {
        SPDLOG_WARN("发送给web_gateway失败: 对端可能未运行，消息已丢弃");
        return;
    }
    SPDLOG_INFO("发送给web_gateway成功: {}", message.dump());
    point_unreachable_streak_reported = true;
}

void Navigator::handle_in_virtual_wall() {
    final_result = NavigationResult::FAILED;
    nav_cv.notify_one();

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "卡虚拟墙"}}}
    };
    auto result = socket_to_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    if (!result) {
        SPDLOG_WARN("发送给web_gateway失败: 对端可能未运行，消息已丢弃");
        return;
    } 
    SPDLOG_INFO("发送给web_gateway成功: {}", message.dump());
}

void Navigator::handle_location_lost(int code, const std::string& msg, std::chrono::steady_clock::time_point now) {
    auto lost_start = (runtime_status.has_value() && runtime_status->localization_lost_start.has_value()) ? *runtime_status->localization_lost_start : now;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lost_start);
    SPDLOG_WARN("定位丢失: {}s/{}s", elapsed.count(), LOCALIZATION_LOST_TIMEOUT.count());

    runtime_status = RuntimeStatus{code, msg, now, lost_start};
    if (elapsed >= LOCALIZATION_LOST_TIMEOUT) {
        SPDLOG_ERROR("定位丢失超时，导航失败");
        final_result = NavigationResult::LOCALIZATION_LOST;
    }
    nav_cv.notify_one();
}

void Navigator::handle_nav_unknown_error(int code, const std::string& msg) {
    SPDLOG_WARN("未识别的导航状态码: {} - {}", code, msg);
    final_result = NavigationResult::FAILED;
    nav_cv.notify_one();

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "导航未知异常"}}}
    };

    auto result = socket_to_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    if (!result) {
        SPDLOG_WARN("发送给web_gateway失败: 对端可能未运行，消息已丢弃");
        return;
    } 
    SPDLOG_INFO("发送给web_gateway成功: {}", message.dump());
}

bool Navigator::resend_navigation() {
    if (!pending_command.has_value()) return false;
    const auto& c = *pending_command;
    SPDLOG_INFO("定位丢失中，重发导航命令");
    
    httplib::Result resp;
    if (c.is_custom) {
        nlohmann::json params = {
            {"destination", {
                {"mapName", c.map_name},
                {"angle", c.angle},
                {"gridPosition", {{"x", c.x}, {"y", c.y}}}
            }}
        };
        resp = http->Post(base_url + "/cmd/navigate", params.dump(), "application/json");
    } else {
        resp = http->Get(
            base_url + "/cmd/position/navigate?map_name=" + 
            c.map_name + "&position_name=" + c.point_name
        );
    }
    return resp && resp->status == 200;
}

void Navigator::sync_control(bool do_cancel, bool do_pause) {
    cancel.store(do_cancel);
    paused.store(do_pause);
    nav_cv.notify_one();
}

bool Navigator::cancel_navigation() {
    auto resp = http->Get(base_url + "/cmd/cancel_navigate");
    return resp && resp->status == 200 && nlohmann::json::parse(resp->body).value("successed", false);
}

bool Navigator::pause_navigation() {
    SPDLOG_INFO("暂停");
    auto resp = http->Get(base_url + "/cmd/pause_navigate");
    return resp && resp->status == 200 && nlohmann::json::parse(resp->body).value("successed", false);
}

bool Navigator::resume_navigation() {
    SPDLOG_INFO("恢复");
    auto resp = http->Get(base_url + "/cmd/resume_navigate");
    return resp && resp->status == 200 && nlohmann::json::parse(resp->body).value("successed", false);
}

bool Navigator::is_navigating() const {
    return navigating.load();
}

bool Navigator::is_paused() const {
    return paused.load();
}
