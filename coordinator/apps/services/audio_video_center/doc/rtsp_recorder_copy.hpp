#pragma once
#include <string>
#include <atomic>
#include <mutex>

extern "C" {
    #include <libavformat/avformat.h>
}

class RtspRecorder {
public:
    struct Config {
        std::string rtsp_url;
        std::string output_dir;
        std::string file_prefix;
        int segment_duration_sec = 1200;
        uint64_t disk_critical_percent = 80;
        std::string disk_mount_point = "/datas";
    };
    struct Stats {
        int64_t total_bytes = 0;
        int64_t current_file_bytes = 0;
        int files_created = 0;
        std::string current_file;
    };

    RtspRecorder();
    ~RtspRecorder();

    // 返回 true 表示曾经成功录像过（连接成功），false 表示启动就失败了
    bool run(const Config& config);

    // 请求停止（线程安全，可从其他线程调用）
    void request_stop();

    // 获取最后一条错误信息 
    std::string get_last_error() const;

private:
    struct DiskInfo {
        uint64_t total_mb = 0;
        uint64_t available_mb = 0;
        uint64_t used_percent = 0;
    };

    Config config_;
    AVFormatContext* input_ctx_ = nullptr;
    AVFormatContext* output_ctx_ = nullptr;
    int video_stream_idx_ = -1;
    std::string last_error_;

    std::atomic<bool> stop_requested_{false};

    mutable std::mutex stats_mutex_;
    Stats stats_;
    std::chrono::steady_clock::time_point segment_start_time_;

    bool open_input();
    bool open_output(const std::string& filename);
    void close_input();
    void close_output();

    DiskInfo get_disk_info(const std::string& path);
    bool should_stop_recording();
    void cleanup();
    std::string generate_filename();
    void remux_to_standard_mp4(const std::string& frag_file);
};