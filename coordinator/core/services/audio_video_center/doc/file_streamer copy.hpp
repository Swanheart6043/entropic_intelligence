#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

/**
 * 文件推流器：将本地 MP4 文件推流到 RTSP 服务器
 */
class FileStreamer {
public:
    FileStreamer() = default;
    ~FileStreamer();

    FileStreamer(const FileStreamer&) = delete;
    FileStreamer& operator=(const FileStreamer&) = delete;

    /**
     * 启动推流
     * @param file_path  本地文件路径，如 /data/video/test.mp4
     * @param output_url 输出 RTSP 地址，如 rtsp://server:8554/live/test
     * @return 是否启动成功
     */
    bool start(const std::string& file_path, const std::string& output_url);

    void stop();

    bool is_running() const { return running_.load(); }

    struct Stats {
        uint64_t frames_processed = 0;
        uint64_t bytes_sent = 0;
        int64_t write_errors = 0;
    };

    Stats get_stats() const;

    /// 推流自然结束回调（文件播完时触发，非 stop 调用）
    using FinishCallback = std::function<void()>;
    void set_finish_callback(FinishCallback cb) { finish_callback_ = std::move(cb); }

private:
    bool open_input(const std::string& file_path);
    bool open_output(const std::string& url);
    void cleanup();
    void stream_thread_func(const std::string& file_path, const std::string& output_url);

    static int interrupt_callback(void* ctx) {
        auto* self = static_cast<FileStreamer*>(ctx);
        return self->stop_requested_.load() ? 1 : 0;
    }

    AVFormatContext* input_ctx_ = nullptr;
    AVFormatContext* output_ctx_ = nullptr;
    int video_stream_idx_ = -1;

    std::thread stream_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};

    mutable std::mutex stats_mutex_;
    Stats stats_;

    FinishCallback finish_callback_;
};