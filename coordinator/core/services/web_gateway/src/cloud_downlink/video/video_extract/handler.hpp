#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "extractor.hpp"

class ExtractorHandler {
public:
    explicit ExtractorHandler(WebSocketClient& websocket_connect);
    ~ExtractorHandler();

    void start();
    void stop();
    bool submit(const std::string& message);

private:
    struct ExtractionTask {
        std::string channel;
        std::string start_time;
        std::string end_time;
        std::string uuid;
    };

    static constexpr std::size_t max_pending_tasks = 1;

    WebSocketClient& websocket;
    Config* config = Config::getInstance();
    std::string upload_path;

    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::mutex queue_mutex;
    std::condition_variable queue_condition;
    std::deque<ExtractionTask> pending_tasks;

    VideoTimeRangeExtractor video_time_range_extractor;

    void worker_loop();
    void process_task(const ExtractionTask& task);
    bool parse_task(const std::string& message, ExtractionTask& task, std::string& error_message) const;
    void send_ack();
    bool upload_file_to_server(const std::string& file_path, const std::string& uuid);
};
