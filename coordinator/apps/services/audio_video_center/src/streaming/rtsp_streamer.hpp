#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
}

class RtspStreamer {
public:
    struct Stats {
        uint64_t frames_processed = 0;
        uint64_t bytes_sent = 0;
        double fps = 0.0;
        int64_t bitrate = 0;
        int64_t write_errors = 0; // 写入错误数
        int64_t read_errors = 0; // 读取错误数
        int64_t last_write_ts = 0; // 最后写入时间戳
    };
    // 禁用拷贝
    RtspStreamer(const RtspStreamer&) = delete;
    RtspStreamer& operator=(const RtspStreamer&) = delete;
    
    RtspStreamer() = default;
    ~RtspStreamer();

    bool start(const std::string& input_url, const std::string& output_url);
    void stop();

    Stats get_stats() const;
    bool is_running() const { return running_.load(); }

private:
    // FFmpeg 上下文
    AVFormatContext* input_ctx_ = nullptr;
    AVFormatContext* output_ctx_ = nullptr;
    int video_stream_idx_ = -1;

    // 线程控制
    std::thread stream_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    
    // 统计信息
    mutable std::mutex stats_mutex_;
    Stats stats_;

    // 中断回调
    static int interrupt_callback(void* ctx) {
        RtspStreamer* streamer = static_cast<RtspStreamer*>(ctx);
        return streamer->stop_requested_.load() ? 1 : 0;
    }
    bool open_input(const std::string& url);
    bool open_output(const std::string& url);
    void cleanup();
    void stream_thread_func(const std::string& input_url, const std::string& output_url);
};
