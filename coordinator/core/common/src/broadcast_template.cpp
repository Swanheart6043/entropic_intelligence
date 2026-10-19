#include <spdlog/spdlog.h>
#include <utility>
#include "config_parser.hpp"
#include "db_connection.hpp"
#include "broadcast_template.hpp"

BroadcastTemplate::BroadcastTemplate(std::string event_key) : event_key_(std::move(event_key)) {
    
}

std::string BroadcastTemplate::get_content(const std::string& default_value) {
    auto now = std::chrono::steady_clock::now();
    {
        std::shared_lock lock(cache_mtx_);
        if ((now - fetched_at_) < TTL) {
            return content_;
        }
    }
    try {
        auto row = query_db();
        if (row) {
            std::unique_lock lock(cache_mtx_);
            content_ = *row;
            fetched_at_ = now;
            return *row;
        }
    } catch (const sql::SQLException& e) {
        SPDLOG_WARN("查询播报模板失败 [{}]: {},使用默认值", event_key_, e.what());
    }
    return default_value;
}

std::optional<std::string> BroadcastTemplate::query_db() const {
    auto* conn = DBConnection::get_instance().get_connection();
    auto sql = "SELECT content FROM broadcast_template WHERE event_key = ? AND lang = ?";

    std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
    stmt->setString(1, event_key_);
    stmt->setString(2, Config::getInstance()->get_language());
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    if (rs->next()) {
        return rs->getString("content");
    }
    return std::nullopt;
}