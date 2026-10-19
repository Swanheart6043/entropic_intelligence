#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <chrono>
#include <functional>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

/**
 * 文件推流器：将本地 MP4 文件推流到 RTSP 服务器
 * 支持多流(video + audio)：音频原样 remux，不解码不降噪（CPU 零开销）。
 * 降噪在离线导出/截取环节单独处理，不在推流路径做。
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

    // 【本次改动】一路被推流的流（视频或音频）的映射与时间戳状态。
    // 原来只推单路视频，时间戳用 wall_start 重算；现在多流后改为源 PTS 重建 + 单调钳制，
    // 每路流独立保存 time_base 与上次写出的 dts。
    struct StreamMapping {
        int in_idx = -1;                       // 输入流索引
        int out_idx = -1;                      // 输出流索引
        AVRational in_tb{};                    // 输入 time_base
        AVRational out_tb{};                   // 输出 time_base（write_header 后才确定）
        int64_t last_out_dts = AV_NOPTS_VALUE; // 上次写出的 dts（输出 timebase），单调钳制用
    };

    AVFormatContext* input_ctx_ = nullptr;
    AVFormatContext* output_ctx_ = nullptr;

    int video_stream_idx_ = -1;                // 第一路视频流的输入索引（等关键帧用）
    // 【本次改动】多流支持
    std::vector<StreamMapping> stream_maps_;   // 所有被推流的流（按加入顺序）
    std::vector<int> in_to_map_;               // input stream idx -> stream_maps_ 下标，-1 表示丢弃

    bool header_written_ = false;              // 【本次改动】write_header 成功后置 true，控制 trailer 是否可写

    std::thread stream_thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    mutable std::mutex stats_mutex_;
    Stats stats_;
    FinishCallback finish_callback_;
};