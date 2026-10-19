#include "cloud_api/cloud_outbox.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using Json = nlohmann::json;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Sandbox {
    fs::path path = fs::temp_directory_path() / ("cloud-outbox-regression-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    Sandbox() { fs::create_directories(path); }
    ~Sandbox() {
        if (path.parent_path() == fs::temp_directory_path() &&
            path.filename().string().find("cloud-outbox-regression-") == 0) {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    }
};

std::string id(int number) { return std::string(16 - std::to_string(number).size(), '0') + std::to_string(number); }
Json request(int number, bool alarm = false) {
    return {{"version", 1}, {"request_id", id(number)},
            {"operation", alarm ? "enqueue_alarm_report" : "enqueue_capture_report"},
            {"payload", {{"point_name", "point"}}}};
}
bool enqueue(CloudOutbox& store, int number, bool alarm = false, std::size_t bytes = 100) {
    std::string code, message;
    return store.enqueue(request(number, alarm), std::string(bytes, 'x'), code, message);
}
CloudOutboxPolicy policy() {
    CloudOutboxPolicy p;
    p.max_jobs = 4;
    p.max_megabytes = 2;
    p.alarm_reserved_jobs = 1;
    p.alarm_reserved_megabytes = 0;
    p.orphan_grace_seconds = 1;
    return p;
}
void put(const fs::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }
void age(const fs::path& path) {
    fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(200));
}

void reservation_eviction_and_idempotency() {
    Sandbox dir;
    CloudOutbox store(dir.path, policy());
    store.start();
    require(enqueue(store, 1) && enqueue(store, 2) && enqueue(store, 3), "initial captures");
    auto pinned = store.claim_due();
    require(pinned && pinned->metadata.at("request_id") == id(1), "oldest capture first");
    require(enqueue(store, 4), "new capture must evict oldest unpinned capture");
    require(fs::exists(dir.path / (id(1) + ".bin")), "active image must stay pinned");
    require(!fs::exists(dir.path / (id(2) + ".json")), "oldest unpinned capture evicted");
    require(enqueue(store, 5, true), "alarm can use its reserved slot");
    require(enqueue(store, 5, true), "same request_id succeeds when full");
    store.release(*pinned);
    auto alarm = store.claim_due();
    require(alarm && alarm->metadata.at("request_id") == id(5), "alarm priority");
    store.complete(*alarm);
    store.release(*alarm);
    require(!fs::exists(dir.path / (id(5) + ".bin")), "successful image removed");
    require(!fs::exists(dir.path / (id(5) + ".json")), "successful metadata removed");
}

void independent_retry_and_restart() {
    Sandbox dir;
    CloudOutbox store(dir.path, policy());
    store.start();
    require(enqueue(store, 1, true) && enqueue(store, 2), "retry fixtures");
    auto job = store.claim_due();
    job->metadata["next_attempt_at_ms"] = 9000000000000LL;
    job->metadata["pic_url"] = "https://example.test/already-uploaded.jpg";
    require(store.checkpoint(*job), "retry checkpoint");
    store.release(*job);
    auto next = store.claim_due();
    require(next && next->metadata.at("request_id") == id(2), "one retry must not pause other events");
    store.release(*next);
    CloudOutbox restarted(dir.path, policy());
    restarted.start();
    next = restarted.claim_due();
    require(next && next->metadata.at("request_id") == id(2), "retry deadline survives restart");
    std::ifstream metadata(dir.path / (id(1) + ".json"));
    Json persisted;
    metadata >> persisted;
    require(persisted.at("pic_url") == job->metadata.at("pic_url"), "uploaded URL survives restart");
}

void dead_letter_retention_and_pressure() {
    Sandbox dir;
    auto p = policy();
    p.dead_letter_max_jobs = 1;
    CloudOutbox store(dir.path, p);
    store.start();
    for (int n = 1; n <= 2; ++n) {
        require(enqueue(store, n, true), "dead letter fixture");
        auto job = store.claim_due();
        store.fail(*job, "HTTP 400");
        store.release(*job);
        if (n == 1) fs::last_write_time(dir.path / (id(n) + ".failed"),
            fs::file_time_type::clock::now() - std::chrono::seconds(1));
    }
    store.maintain(true);
    require(!fs::exists(dir.path / (id(1) + ".failed")), "oldest dead metadata removed at count limit");
    require(!fs::exists(dir.path / (id(1) + ".bin")), "oldest dead image removed together");
    require(fs::exists(dir.path / (id(2) + ".failed")), "recent dead letter retained");
    require(!store.claim_due(), "dead letters are never sent");
    require(enqueue(store, 3, false, 1400000), "large capture fixture");
    require(enqueue(store, 4, true, 600000), "pressure can reclaim dead letters");
    // Fill the remaining budget so pressure must remove a retained dead image.
    require(enqueue(store, 5, true, 100000), "pressure reclaims dead letters before captures");
    require(!fs::exists(dir.path / (id(2) + ".failed")), "dead letter reclaimed under pressure");
}

void completed_cleanup_failure_and_recovery() {
    Sandbox dir;
    CloudOutbox store(dir.path, policy());
    store.start();
    require(enqueue(store, 1), "completed fixture");
    auto job = store.claim_due();
    const auto binary = dir.path / (id(1) + ".bin");
    fs::remove(binary);
    fs::create_directory(binary);
    put(binary / "blocked", "simulate deletion failure");
    store.complete(*job);
    store.release(*job);
    require(fs::exists(dir.path / (id(1) + ".sent")), "cleanup failure retains sent marker");
    require(!store.claim_due(), "cleanup failure must not resend event");
    fs::remove(binary / "blocked");
    CloudOutbox restarted(dir.path, policy());
    restarted.start();
    require(!fs::exists(binary) && !fs::exists(dir.path / (id(1) + ".sent")), "restart resumes cleanup");
}

void expiry_orphans_and_invalid_metadata() {
    Sandbox dir;
    CloudOutbox store(dir.path, policy());
    store.start();
    require(enqueue(store, 1) && enqueue(store, 2, true), "expiry fixtures");
    auto alarm = store.claim_due();
    alarm->metadata["created_at_ms"] = 1;
    require(store.checkpoint(*alarm), "old alarm checkpoint");
    store.release(*alarm);
    auto capture_path = dir.path / (id(1) + ".json");
    auto capture = CloudOutbox::Job{capture_path, {}};
    std::ifstream stream(capture_path);
    stream >> capture.metadata;
    stream.close();
    capture.metadata["created_at_ms"] = 1;
    require(store.checkpoint(capture), "old capture checkpoint");
    put(dir.path / (id(3) + ".bin"), "orphan");
    put(dir.path / (id(4) + ".bin.tmp"), "interrupted write");
    put(dir.path / (id(5) + ".bin.tmp"), "recent temporary");
    age(dir.path / (id(3) + ".bin"));
    age(dir.path / (id(4) + ".bin.tmp"));
    put(dir.path / (id(6) + ".json"), "{broken");
    put(dir.path / (id(6) + ".bin"), "dead image");
    put(dir.path / (id(7) + ".failed"), "{}");
    put(dir.path / (id(7) + ".bin"), "expired dead image");
    age(dir.path / (id(7) + ".failed"));
    store.maintain(true);
    require(!fs::exists(capture_path), "expired capture discarded");
    require(fs::exists(dir.path / (id(2) + ".json")), "old alarm is not expired");
    require(!fs::exists(dir.path / (id(3) + ".bin")), "orphan removed");
    require(!fs::exists(dir.path / (id(4) + ".bin.tmp")), "old temporary removed");
    require(fs::exists(dir.path / (id(5) + ".bin.tmp")), "recent temporary preserved");
    require(fs::exists(dir.path / (id(6) + ".failed")), "corrupted metadata quarantined");
    require(!fs::exists(dir.path / (id(7) + ".failed")) &&
            !fs::exists(dir.path / (id(7) + ".bin")), "dead TTL removes both files");
}

void concurrent_cleanup_and_checkpoint_failure() {
    Sandbox dir;
    CloudOutbox store(dir.path, policy());
    store.start();
    require(enqueue(store, 1), "checkpoint failure fixture");
    auto job = store.claim_due();
    const auto temporary = job->path.string() + ".tmp";
    fs::create_directory(temporary); // Opening the checkpoint temporary must fail.
    job->metadata["next_attempt_at_ms"] = 9000000000000LL;
    require(!store.checkpoint(*job), "checkpoint failure must be visible");
    store.release(*job);
    require(!store.claim_due(), "failed retry checkpoint must not cause busy retry");
    require(fs::exists(job->path), "failed atomic replacement preserves committed metadata");
    fs::remove(temporary);
    store.complete(*job);

    std::atomic<bool> done{false};
    std::atomic<int> errors{0};
    std::thread producer([&] {
        try {
            for (int n = 10; n < 110; ++n) {
                if (!enqueue(store, n)) ++errors;
                if (n % 5 == 0) store.maintain(true);
            }
        } catch (...) { ++errors; }
        done = true;
    });
    do {
        auto active = store.claim_due();
        if (active) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            try {
                if (store.read_image(*active) != std::string(100, 'x')) ++errors;
                store.complete(*active);
            } catch (...) { ++errors; }
            store.release(*active);
        }
    } while (!done);
    producer.join();
    require(errors == 0, "concurrent enqueue / cleanup must preserve active images");
}

void byte_limit_and_alarm_protection() {
    Sandbox dir;
    auto p = policy();
    p.max_megabytes = 1;
    CloudOutbox store(dir.path, p);
    store.start();
    require(!enqueue(store, 1, false, 1024 * 1024), "capacity must include metadata");
    require(enqueue(store, 2, true, 700000), "alarm fixture");
    require(!enqueue(store, 3, false, 700000), "capture cannot evict alarm to gain bytes");
    require(fs::exists(dir.path / (id(2) + ".bin")), "alarm preserved when full");
}

int main() {
    try {
        spdlog::set_level(spdlog::level::warn);
        reservation_eviction_and_idempotency();
        independent_retry_and_restart();
        dead_letter_retention_and_pressure();
        completed_cleanup_failure_and_recovery();
        expiry_orphans_and_invalid_metadata();
        byte_limit_and_alarm_protection();
        concurrent_cleanup_and_checkpoint_failure();
        std::cout << "PASS: 7 outbox regression scenarios\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
