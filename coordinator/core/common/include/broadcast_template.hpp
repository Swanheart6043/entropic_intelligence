#pragma once
#include <string>
#include <optional>
#include <shared_mutex>
#include <chrono>

class BroadcastTemplate {
public:
    explicit BroadcastTemplate(std::string event_key);
    std::string get_content(const std::string& default_value);

private:
    const std::string event_key_;
    std::string content_;
    std::chrono::steady_clock::time_point fetched_at_{};
    std::shared_mutex cache_mtx_;
    static constexpr auto TTL = std::chrono::minutes(2);

    std::optional<std::string> query_db() const;
};