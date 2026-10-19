#pragma once
#include <string>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
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

    // 【本次改动】一路被录制流（视频或音频）的映射与时间戳状态。
    // 时间戳策略：纯计数器模式 —— 彻底不信任源 pts/dts（某些摄像头如红外每帧都异常），
    // 输出时间戳 = 帧序号 × 每帧步长。匀速、单调、每段归零，永不溢出、永不刷屏。
    // 代价：放弃源时间戳的精确节奏，音视频可能有轻微漂移；巡逻存档场景可接受。
    struct StreamMapping {
        int in_idx = -1;                      // 输入流索引
        int out_idx = -1;                     // 输出流索引
        AVRational in_tb{};                   // 输入 time_base
        AVRational out_tb{};                  // 输出 time_base（write_header 后才确定）
        int64_t step = 0;                     // 每包步长（out_tb 下）：视频按帧率，音频按每包采样数
        int64_t frame_index = 0;              // 本段内该流已写出的包数，open_output 时归零
    };

    Config config_;
    AVFormatContext* input_ctx_ = nullptr;
    AVFormatContext* output_ctx_ = nullptr;

    int video_stream_idx_ = -1;               // 第一路视频流的输入索引（用于分段切点判断）
    // 【本次改动】多流支持
    std::vector<StreamMapping> stream_maps_;  // 所有被录制的流（按加入顺序）
    std::vector<int> in_to_map_;              // input stream idx -> stream_maps_ 下标，-1 表示丢弃

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