#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "config_parser.hpp"

// File mutations and housekeeping share one lock. A claimed event stays pinned
// while the caller performs network I/O without holding that lock.
class CloudOutbox {
public:
    struct Job {
        std::filesystem::path path;
        nlohmann::json metadata;
    };

    CloudOutbox(std::filesystem::path directory, CloudOutboxPolicy policy);
    void start();
    bool enqueue(const nlohmann::json& request, const std::string& image, std::string& error_code, std::string& error_message);
    std::optional<Job> claim_due();
    void release(const Job& job);
    std::string read_image(const Job& job) const;
    bool checkpoint(const Job& job);
    void complete(const Job& job);
    void fail(const Job& job, const std::string& reason);
    void maintain(bool force = false);

private:
    struct Usage {
        std::size_t jobs{0};
        std::size_t captures{0};
        std::uintmax_t bytes{0};
        std::uintmax_t capture_bytes{0};
    };
    std::filesystem::path directory_;
    CloudOutboxPolicy policy_;
    std::mutex mutex_;
    std::filesystem::path active_;
    // GCC 9 libstdc++ lacks std::hash<filesystem::path>; ordered path keys also
    // preserve filesystem path comparison without relying on that specialization.
    std::map<std::filesystem::path, std::int64_t> retry_fallback_;
    std::chrono::steady_clock::time_point next_cleanup_{};
    std::uint64_t discarded_captures_{0};

    Usage usage_locked() const;
    std::vector<Job> pending_locked();
    bool remove_file_locked(const std::filesystem::path& path);
    bool remove_pair_locked(const std::filesystem::path& metadata, const std::string& reason);
    void fail_locked(const Job& job, const std::string& reason);
    void maintain_locked(bool pressure);
    bool fits_locked(const Usage& usage, std::uintmax_t incoming, bool capture) const;
};
