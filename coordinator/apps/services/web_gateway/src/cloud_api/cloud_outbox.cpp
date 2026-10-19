#include "cloud_outbox.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <spdlog/spdlog.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {
constexpr std::uintmax_t kMiB = 1024U * 1024U;
// Headroom for pic_url / retry checkpoints, included in admission accounting.
constexpr std::uintmax_t kMetadataReserve = 8192;

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool safe_id(const std::string& id) {
    static const std::regex pattern("^[a-fA-F0-9-]{16,64}$");
    return std::regex_match(id, pattern);
}

Json read_json(const fs::path& path) {
    if (fs::file_size(path) > 64 * 1024) throw std::runtime_error("outbox metadata exceeds 64 KiB");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read outbox metadata: " + path.string());
    Json value;
    stream >> value;
    return value;
}

bool write_atomic(const fs::path& path, const std::string& data) {
    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
        stream.flush();
        if (!stream) return false;
    }
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    fs::rename(temporary, path, ec);
    return !ec; // Never remove a committed checkpoint if replacement fails.
#endif
}

bool old_file(const fs::path& path, std::chrono::seconds age) {
    return fs::file_time_type::clock::now() - fs::last_write_time(path) >= age;
}

fs::path image_path(const fs::path& metadata) {
    auto path = metadata;
    path.replace_extension(".bin");
    return path;
}

bool is_capture(const Json& job) {
    return job.value("operation", "") == "enqueue_capture_report";
}
}

CloudOutbox::CloudOutbox(fs::path directory, CloudOutboxPolicy policy)
    : directory_(std::move(directory)), policy_(policy) {}

void CloudOutbox::start() {
    fs::create_directories(directory_);
    maintain(true);
}

CloudOutbox::Usage CloudOutbox::usage_locked() const {
    Usage usage;
    for (const auto& entry : fs::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.is_symlink()) continue;
        const auto size = entry.file_size();
        usage.bytes += size; // Includes dead letters, images, .sent and .tmp.
        if (entry.path().extension() != ".json") continue;
        ++usage.jobs;
        usage.bytes += size < kMetadataReserve ? kMetadataReserve - size : 0;
        try {
            if (is_capture(read_json(entry.path()))) {
                ++usage.captures;
                const auto binary = image_path(entry.path());
                usage.capture_bytes += std::max(size, kMetadataReserve);
                if (fs::is_regular_file(binary) && !fs::is_symlink(binary)) {
                    usage.capture_bytes += fs::file_size(binary);
                }
            }
        } catch (...) { /* Invalid metadata remains included in total usage. */ }
    }
    return usage;
}

bool CloudOutbox::fits_locked(const Usage& usage, std::uintmax_t incoming, bool capture) const {
    if (usage.jobs >= static_cast<std::size_t>(policy_.max_jobs) ||
        usage.bytes + incoming > static_cast<std::uintmax_t>(policy_.max_megabytes) * kMiB) return false;
    return !capture ||
        (usage.captures < static_cast<std::size_t>(policy_.max_jobs - policy_.alarm_reserved_jobs) &&
         usage.capture_bytes + incoming <=
             static_cast<std::uintmax_t>(policy_.max_megabytes - policy_.alarm_reserved_megabytes) * kMiB);
}

bool CloudOutbox::enqueue(const Json& request, const std::string& image,
                         std::string& error_code, std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);
    fs::path metadata;
    fs::path binary;
    try {
        const auto id = request.at("request_id").get<std::string>();
        if (!safe_id(id)) throw std::invalid_argument("invalid outbox request_id");
        metadata = directory_ / (id + ".json");
        binary = directory_ / (id + ".bin");
        // Check idempotency before capacity; a full queue can still accept a retry.
        if (fs::exists(metadata) || fs::exists(directory_ / (id + ".sent"))) return true;
        if (fs::exists(directory_ / (id + ".failed"))) {
            error_code = "event_dead_lettered";
            error_message = "request_id already belongs to a dead letter";
            return false;
        }
        const Json job = {
            {"version", request.at("version")}, {"request_id", id},
            {"operation", request.at("operation")}, {"payload", request.at("payload")},
            {"image_file", binary.filename().string()}, {"attempts", 0},
            {"next_attempt_at_ms", 0}, {"created_at_ms", now_ms()}
        };
        const std::string encoded = job.dump();
        const auto incoming = image.size() + std::max<std::uintmax_t>(encoded.size(), kMetadataReserve);
        const bool capture = is_capture(job);
        const auto event_budget = static_cast<std::uintmax_t>(policy_.max_megabytes -
            (capture ? policy_.alarm_reserved_megabytes : 0)) * kMiB;
        if (incoming > event_budget || encoded.size() > kMetadataReserve / 2) {
            error_code = "payload_too_large";
            error_message = "image and metadata exceed the event capacity limit";
            return false;
        }
        auto usage = usage_locked();
        if (!fits_locked(usage, incoming, capture)) {
            maintain_locked(true);
            usage = usage_locked();
            for (const auto& old : pending_locked()) {
                if (fits_locked(usage, incoming, capture)) break;
                if (old.path == active_ || !is_capture(old.metadata)) continue;
                if (remove_pair_locked(old.path, "capacity_pressure")) ++discarded_captures_;
                usage = usage_locked();
            }
        }
        if (!fits_locked(usage, incoming, capture)) {
            error_code = "outbox_full";
            error_message = "cloud outbox full: jobs=" + std::to_string(usage.jobs) +
                "/" + std::to_string(policy_.max_jobs) + ", bytes=" + std::to_string(usage.bytes) +
                ", incoming=" + std::to_string(incoming);
            SPDLOG_INFO("云端outbox拒绝入队: request_id={}, {}", id, error_message);
            return false;
        }
        if (!write_atomic(binary, image) || !write_atomic(metadata, encoded)) {
            throw std::runtime_error("cannot persist outbox event");
        }
        return true;
    } catch (const std::exception& e) {
        if (!metadata.empty()) {
            remove_file_locked(metadata.string() + ".tmp");
            remove_file_locked(binary.string() + ".tmp");
            if (!fs::exists(metadata)) remove_file_locked(binary);
        }
        error_code = "outbox_write_failed";
        error_message = e.what();
        return false;
    }
}

std::vector<CloudOutbox::Job> CloudOutbox::pending_locked() {
    std::vector<Job> jobs;
    for (const auto& entry : fs::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.is_symlink() || entry.path().extension() != ".json" ||
            !safe_id(entry.path().stem().string())) continue;
        try {
            auto metadata = read_json(entry.path());
            const auto id = entry.path().stem().string();
            if (metadata.at("request_id").get<std::string>() != id ||
                metadata.at("image_file").get<std::string>() != id + ".bin" ||
                !metadata.at("payload").is_object()) throw std::runtime_error("invalid outbox metadata");
            const auto operation = metadata.at("operation").get<std::string>();
            if (operation != "enqueue_capture_report" && operation != "enqueue_alarm_report") {
                throw std::runtime_error("unsupported outbox operation");
            }
            metadata.at("created_at_ms").get<std::int64_t>();
            metadata.value("next_attempt_at_ms", 0LL);
            metadata.value("attempts", 0);
            metadata.value("pic_url", "");
            jobs.push_back({entry.path(), std::move(metadata)});
        } catch (const std::exception& e) {
            if (entry.path() != active_) fail_locked({entry.path(), Json::object()}, e.what());
        }
    }
    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
        const auto at = a.metadata.at("created_at_ms").get<std::int64_t>();
        const auto bt = b.metadata.at("created_at_ms").get<std::int64_t>();
        return at == bt ? a.path < b.path : at < bt;
    });
    return jobs;
}

std::optional<CloudOutbox::Job> CloudOutbox::claim_due() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_.empty()) return std::nullopt;
    // Oldest due alarm first, then oldest due capture; retries block only themselves.
    auto jobs = pending_locked();
    for (const bool capture : {false, true}) {
        for (const auto& job : jobs) {
            if (is_capture(job.metadata) == capture &&
                job.metadata.value("next_attempt_at_ms", 0LL) <= now_ms() &&
                (retry_fallback_.find(job.path) == retry_fallback_.end() ||
                 retry_fallback_.at(job.path) <= now_ms())) {
                active_ = job.path;
                return job;
            }
        }
    }
    return std::nullopt;
}

void CloudOutbox::release(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ == job.path) active_.clear();
}

std::string CloudOutbox::read_image(const Job& job) const {
    const auto path = image_path(job.path);
    if (fs::is_symlink(path)) throw std::runtime_error("outbox image must not be a symlink");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read outbox image: " + path.string());
    std::string image(std::istreambuf_iterator<char>(stream), {});
    if (stream.bad()) throw std::runtime_error("outbox image read failed: " + path.string());
    return image;
}

bool CloudOutbox::checkpoint(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool saved = write_atomic(job.path, job.metadata.dump());
    if (saved) retry_fallback_.erase(job.path);
    else retry_fallback_[job.path] = std::max<std::int64_t>(
        job.metadata.value("next_attempt_at_ms", 0LL), now_ms() + 5000);
    return saved;
}

bool CloudOutbox::remove_file_locked(const fs::path& path) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) SPDLOG_WARN("清理云端outbox文件失败: file={}, err={}", path.string(), ec.message());
    return !ec;
}

bool CloudOutbox::remove_pair_locked(const fs::path& metadata, const std::string& reason) {
    // Keep metadata until the image is gone, so interrupted cleanup is resumable.
    if (!remove_file_locked(image_path(metadata)) || !remove_file_locked(metadata)) return false;
    retry_fallback_.erase(metadata);
    SPDLOG_INFO("清理云端outbox事件: file={}, reason={}", metadata.filename().string(), reason);
    return true;
}

void CloudOutbox::complete(const Job& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto sent = job.path;
    sent.replace_extension(".sent");
    std::error_code ec;
    fs::rename(job.path, sent, ec);
    if (ec) {
        retry_fallback_[job.path] = now_ms() + 5000;
        SPDLOG_ERROR("云端outbox本地发送完成但标记失败，后续可能重复发送: file={}, err={}",
                     job.path.string(), ec.message());
        return;
    }
    retry_fallback_.erase(job.path);
    // A surviving .sent is cleanup-only, including after a process restart.
    remove_pair_locked(sent, "local_write_completed");
}

void CloudOutbox::fail_locked(const Job& job, const std::string& reason) {
    try {
        auto metadata = read_json(job.path);
        metadata["dead_letter_reason"] = reason.substr(0, 512);
        metadata["dead_letter_at_ms"] = now_ms();
        if (!write_atomic(job.path, metadata.dump())) {
            SPDLOG_WARN("写入云端outbox死信原因失败: file={}", job.path.string());
        }
    } catch (...) { /* Preserve malformed metadata as evidence too. */ }
    auto failed = job.path;
    failed.replace_extension(".failed");
    std::error_code ec;
    fs::rename(job.path, failed, ec);
    if (ec) {
        retry_fallback_[job.path] = now_ms() + 5000;
        SPDLOG_ERROR("云端outbox转死信失败: file={}, err={}", job.path.string(), ec.message());
        return;
    }
    retry_fallback_.erase(job.path);
    SPDLOG_ERROR("云端outbox事件进入死信: file={}, reason={}", failed.string(), reason);
}

void CloudOutbox::fail(const Job& job, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    fail_locked(job, reason);
}

void CloudOutbox::maintain_locked(bool pressure) {
    for (const auto& job : pending_locked()) {
        if (job.path != active_ && is_capture(job.metadata) &&
            now_ms() - job.metadata.at("created_at_ms").get<std::int64_t>() >=
                policy_.capture_ttl_hours * 3600000LL) {
            if (remove_pair_locked(job.path, "capture_expired")) ++discarded_captures_;
        }
    }
    std::vector<fs::path> dead;
    std::uintmax_t dead_bytes = 0;
    for (const auto& entry : fs::directory_iterator(directory_)) {
        if (!entry.is_regular_file() || entry.is_symlink()) continue;
        const auto path = entry.path();
        const auto ext = path.extension();
        const auto id = path.stem().string();
        if (ext == ".failed" && safe_id(id)) {
            dead.push_back(path);
            dead_bytes += entry.file_size();
            if (fs::is_regular_file(image_path(path))) dead_bytes += fs::file_size(image_path(path));
        } else if (ext == ".sent" && safe_id(id)) {
            remove_pair_locked(path, "completed_event_residue");
        } else if (ext == ".bin" && safe_id(id)) {
            if (!fs::exists(directory_ / (id + ".json")) &&
                !fs::exists(directory_ / (id + ".failed")) &&
                !fs::exists(directory_ / (id + ".sent")) &&
                old_file(path, std::chrono::seconds(policy_.orphan_grace_seconds))) {
                if (remove_file_locked(path)) SPDLOG_INFO("清理云端outbox孤立图片: file={}", path.string());
            }
        } else if (ext == ".tmp") {
            const auto original = path.stem();
            const auto original_ext = original.extension();
            if ((original_ext == ".json" || original_ext == ".bin") &&
                safe_id(original.stem().string()) &&
                directory_ / (original.stem().string() + ".json") != active_ &&
                old_file(path, std::chrono::seconds(policy_.orphan_grace_seconds))) {
                if (remove_file_locked(path)) SPDLOG_INFO("清理云端outbox临时文件: file={}", path.string());
            }
        }
    }
    std::sort(dead.begin(), dead.end(), [](const fs::path& a, const fs::path& b) {
        return fs::last_write_time(a) < fs::last_write_time(b);
    });
    auto dead_count = dead.size();
    for (const auto& path : dead) {
        if (pressure || dead_count > static_cast<std::size_t>(policy_.dead_letter_max_jobs) ||
            dead_bytes > static_cast<std::uintmax_t>(policy_.dead_letter_max_megabytes) * kMiB ||
            old_file(path, std::chrono::hours(policy_.dead_letter_ttl_hours))) {
            auto bytes = fs::file_size(path);
            if (fs::is_regular_file(image_path(path))) bytes += fs::file_size(image_path(path));
            if (remove_pair_locked(path, pressure ? "dead_letter_capacity_pressure" : "dead_letter_retention")) {
                --dead_count;
                dead_bytes -= bytes;
            }
        }
    }
    const auto usage = usage_locked();
    SPDLOG_INFO("云端outbox状态: jobs={}/{}, bytes={}/{}, captures={}, dead_letters={}, discarded_captures={}",
                usage.jobs, policy_.max_jobs, usage.bytes,
                static_cast<std::uintmax_t>(policy_.max_megabytes) * kMiB,
                usage.captures, dead_count, discarded_captures_);
}

void CloudOutbox::maintain(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < next_cleanup_) return;
    maintain_locked(false);
    next_cleanup_ = now + std::chrono::seconds(policy_.cleanup_interval_seconds);
}
