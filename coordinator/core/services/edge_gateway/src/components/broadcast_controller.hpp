#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "config_parser.hpp"

class BroadcastController {
public:
    BroadcastController();
    ~BroadcastController();

    void broadcast_text(const std::string& text, const std::string& play_mode, const std::string& duration);
    void broadcast_single(const std::string& content);
    void broadcast_loop(const std::string& content, const std::string& duration);
    void control_volume(const std::string& volume);
    std::string utf8_to_unicode_escape(const std::string& input);

private: 
    static constexpr int BROADCAST_INTERVAL_SECONDS = 10;

    std::atomic<bool> looping{false};
    std::atomic<bool> cancel{false};

    std::thread worker_thread;
    std::mutex mutex;

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::push};

    Config* config = Config::getInstance();
    nlohmann::json response = {
        {"command", "broadcast-text"},
        {"content", {
            {"Soundtext", ""}, 
            {"Priority", "10"}, 
            {"BroadcastCnt", "1"}, 
            {"BroadcastInv", "0"},
        }}
    };
};