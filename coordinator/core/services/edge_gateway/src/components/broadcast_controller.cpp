#include <spdlog/spdlog.h>
#include "db_connection.hpp"
#include "common/websocket_manager.hpp"
#include "broadcast_controller.hpp"

BroadcastController::BroadcastController() {
    int linger_report = 3000;
    int linger_misc = 1000;
    socket.setsockopt(ZMQ_LINGER, &linger_report, sizeof(linger_report));
    socket.connect("tcp://127.0.0.1:2007");
}

BroadcastController::~BroadcastController() {

}

void BroadcastController::broadcast_text(const std::string& text, const std::string& play_mode, const std::string& duration) {
    if (play_mode == "single") {
        broadcast_single(text);
    }
    if (play_mode == "loop") {
        broadcast_loop(text, duration);
    }
}

void BroadcastController::broadcast_single(const std::string& content) {
    nlohmann::json response = {
        {"command", "broadcast-text"},
        {"content", {
            {"Soundtext", content}, 
            {"Priority", "10"}, 
            {"BroadcastCnt", "1"}, 
            {"BroadcastInv", "0"},
        }}
    };
    WebSocketManager::getInstance().broadcast(response.dump());
    
    nlohmann::json payload = {
        {"Header", {
            {"CmdType", "PhoenixStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"CurrentStep", {
            {"type", 1}, 
            {"value", utf8_to_unicode_escape(content)}
        }}
    };
    socket.send(zmq::buffer(payload.dump()), zmq::send_flags::dontwait);
}

void BroadcastController::broadcast_loop(const std::string& content, const std::string& duration) {
    if (looping.load()) {
        SPDLOG_INFO("有运行中的广播，不处理");
        return;
    }

    // 开始播
    int duration_minutes = 1;
    try {
        duration_minutes = std::stoi(duration);
        if (duration_minutes <= 0) duration_minutes = 1;
    } catch (...) {
        SPDLOG_WARN("duration 解析失败，使用默认值 1 分钟");
    }
    std::lock_guard<std::mutex> lock(mutex);
    looping.store(true);

    worker_thread = std::thread([this, content, duration_minutes]() {
        auto time = std::chrono::steady_clock::now();
        auto end_time = time + std::chrono::minutes(duration_minutes);
        while (time < end_time) {
            time = std::chrono::steady_clock::now();
            response.at("content").at("Soundtext") = content;
            WebSocketManager::getInstance().broadcast(response.dump());
            SPDLOG_INFO("循环广播发送: {}", response.dump());
            std::this_thread::sleep_for(std::chrono::seconds(BROADCAST_INTERVAL_SECONDS));
        }
        looping.store(false);
        SPDLOG_INFO(cancel.load() ? "循环广播已被手动停止" : "循环广播已完成");
    });
    worker_thread.detach();
}

void BroadcastController::control_volume(const std::string& volume) {
	try {
        nlohmann::json response = {{"command", "volume"}, {"content", volume}};
        WebSocketManager::getInstance().broadcast(response.dump());

        sql::Connection* con = DBConnection::get_instance().get_connection();
        auto sql = "UPDATE settings_basic SET config_value = ? WHERE config_key = 'volume'";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        pstmt->setString(1, volume);
        pstmt->executeUpdate();
	} catch (const sql::SQLException& e) {
        SPDLOG_ERROR("数据库更新失败: {} (错误码: {})", e.what(), e.getErrorCode());
    }
}

std::string BroadcastController::utf8_to_unicode_escape(const std::string& input) {
    std::string output;

    for (size_t i = 0; i < input.size();) {
        uint32_t codepoint = 0;
        unsigned char c = input[i];

        if (c <= 0x7F) {
            codepoint = c;
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            codepoint = ((c & 0x1F) << 6) | (input[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            codepoint = ((c & 0x0F) << 12) | ((input[i + 1] & 0x3F) << 6) | (input[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            codepoint = ((c & 0x07) << 18) | ((input[i + 1] & 0x3F) << 12) | ((input[i + 2] & 0x3F) << 6) | (input[i + 3] & 0x3F);
            i += 4;
        }

        char buf[32];
        if (codepoint <= 0xFFFF) {
            snprintf(buf, sizeof(buf), "\\u%04x", codepoint);
            output += buf;
        } else {
            // UTF-16 surrogate pair
            codepoint -= 0x10000;
            uint32_t high = 0xD800 + (codepoint >> 10);
            uint32_t low  = 0xDC00 + (codepoint & 0x3FF);

            snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", high, low);
            output += buf;
        }
    }
    return output;
}