#include <spdlog/spdlog.h>
#include <chrono>
#include "file_streamer.hpp"

FileStreamer::~FileStreamer() {
    stop();
}

bool FileStreamer::start(const std::string& file_path, const std::string& output_url) {
    if (running_.load()) {
        SPDLOG_WARN("文件推流器已在运行");
        return false;
    }

    stop_requested_.store(false);
    running_.store(true);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_ = Stats{};
    }

    stream_thread_ = std::thread(&FileStreamer::stream_thread_func, this, file_path, output_url);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return running_.load();
}

void FileStreamer::stop() {
    if (!running_.load() && !stream_thread_.joinable()) {
        return;
    }

    SPDLOG_INFO("正在停止文件推流...");
    stop_requested_.store(true);

    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }

    running_.store(false);
}

void FileStreamer::stream_thread_func(const std::string& file_path, const std::string& output_url) {
    SPDLOG_INFO("文件推流线程启动: {}", file_path);

    if (!open_input(file_path)) {
        running_.store(false);
        return;
    }

    if (!open_output(output_url)) {
        cleanup();
        running_.store(false);
        return;
    }

    AVPacket* packet = av_packet_alloc();
    int64_t frame_count = 0;
    int64_t bytes_sent = 0;
    int consecutive_write_errors = 0;
    bool got_keyframe = false;
    const int max_write_errors = 50;

    AVStream* in_stream = input_ctx_->streams[video_stream_idx_];
    AVStream* out_stream = output_ctx_->streams[0];
    AVRational in_tb = in_stream->time_base;

    // 速率控制
    std::chrono::steady_clock::time_point wall_start;
    int64_t first_pts_us = 0;
    bool pacing_initialized = false;

    while (!stop_requested_.load()) {
        int ret = av_read_frame(input_ctx_, packet);

        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SPDLOG_INFO("文件播放完毕");
            } else {
                char err_buf[256];
                av_strerror(ret, err_buf, sizeof(err_buf));
                SPDLOG_ERROR("读取文件出错: {}", err_buf);
            }
            av_packet_unref(packet);
            break;
        }

        // 只处理视频流
        if (packet->stream_index != video_stream_idx_) {
            av_packet_unref(packet);
            continue;
        }

        // 等待关键帧
        if (!got_keyframe) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                got_keyframe = true;
                SPDLOG_INFO("收到关键帧，开始推流");
            } else {
                av_packet_unref(packet);
                continue;
            }
        }

        // ====== 速率控制：按 PTS 做 pacing ======
        if (packet->pts != AV_NOPTS_VALUE) {
            int64_t current_pts_us = av_rescale_q(packet->pts, in_tb, {1, 1000000});

            if (!pacing_initialized) {
                first_pts_us = current_pts_us;
                wall_start = std::chrono::steady_clock::now();
                pacing_initialized = true;
            }

            int64_t target_elapsed_us = current_pts_us - first_pts_us;
            int64_t actual_elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - wall_start).count();

            int64_t wait_us = target_elapsed_us - actual_elapsed_us;
            // 分段 sleep，便于响应 stop
            while (wait_us > 1000 && !stop_requested_.load()) {
                int64_t sleep_us = std::min(wait_us, (int64_t)50000);
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));
                wait_us -= sleep_us;
            }
        }

        if (stop_requested_.load()) {
            av_packet_unref(packet);
            break;
        }

        // ====== 生成输出时间戳 ======
        int64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wall_start).count();
        int64_t pts = av_rescale_q(elapsed_us, {1, 1000000}, out_stream->time_base);

        packet->pts = pts;
        packet->dts = pts;
        packet->duration = 0;
        packet->stream_index = 0;
        packet->pos = -1;

        ret = av_interleaved_write_frame(output_ctx_, packet);
        if (ret < 0) {
            if (stop_requested_.load()) {
                av_packet_unref(packet);
                break;
            }

            char err_buf[256];
            av_strerror(ret, err_buf, sizeof(err_buf));

            bool fatal = (ret == AVERROR(EPIPE) || ret == AVERROR_EOF ||
                          ret == AVERROR(ECONNRESET) || ret == AVERROR(ENOTCONN));
            if (fatal) {
                SPDLOG_WARN("连接断开: {}", err_buf);
                av_packet_unref(packet);
                break;
            }

            consecutive_write_errors++;
            SPDLOG_WARN("写入帧失败: {}, 连续错误: {}", err_buf, consecutive_write_errors);

            if (consecutive_write_errors > max_write_errors) {
                SPDLOG_ERROR("写入错误过多");
                av_packet_unref(packet);
                break;
            }

            av_packet_unref(packet);
            continue;
        }

        consecutive_write_errors = 0;
        frame_count++;
        bytes_sent += packet->size;

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.frames_processed = frame_count;
            stats_.bytes_sent = bytes_sent;
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    cleanup();
    running_.store(false);

    SPDLOG_INFO("文件推流结束，共处理 {} 帧", frame_count);

    if (finish_callback_ && !stop_requested_.load()) {
        finish_callback_();
    }
}

bool FileStreamer::open_input(const std::string& file_path) {
    input_ctx_ = avformat_alloc_context();
    input_ctx_->interrupt_callback.callback = interrupt_callback;
    input_ctx_->interrupt_callback.opaque = this;

    int ret = avformat_open_input(&input_ctx_, file_path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("打开文件失败: {} - {}", file_path, err_buf);
        return false;
    }

    ret = avformat_find_stream_info(input_ctx_, nullptr);
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("获取流信息失败: {}", err_buf);
        return false;
    }

    for (unsigned i = 0; i < input_ctx_->nb_streams; i++) {
        if (input_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx_ = i;
            auto* codecpar = input_ctx_->streams[i]->codecpar;
            SPDLOG_INFO("视频流: {}x{}, codec={}",
                codecpar->width, codecpar->height,
                avcodec_get_name(codecpar->codec_id));
            break;
        }
    }

    if (video_stream_idx_ < 0) {
        SPDLOG_ERROR("未找到视频流");
        return false;
    }

    SPDLOG_INFO("文件已打开: {}", file_path);
    return true;
}

bool FileStreamer::open_output(const std::string& url) {
    int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, "rtsp", url.c_str());
    if (ret < 0 || !output_ctx_) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("创建输出上下文失败: {}", err_buf);
        return false;
    }

    AVStream* in_stream = input_ctx_->streams[video_stream_idx_];
    AVStream* out_stream = avformat_new_stream(output_ctx_, nullptr);
    if (!out_stream) {
        SPDLOG_ERROR("创建输出流失败");
        return false;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        SPDLOG_ERROR("复制编码参数失败");
        return false;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "muxdelay", "0", 0);
    av_dict_set(&opts, "muxpreload", "0", 0);

    ret = avformat_write_header(output_ctx_, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("写入头部失败: {}", err_buf);
        return false;
    }

    SPDLOG_INFO("输出已打开: {}", url);
    return true;
}

void FileStreamer::cleanup() {
    if (output_ctx_) {
        if (output_ctx_->pb) {
            avio_flush(output_ctx_->pb);
        }
        if (output_ctx_->pb || !(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
            av_write_trailer(output_ctx_);
        }
        if (output_ctx_->pb && !(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&output_ctx_->pb);
        }
        avformat_free_context(output_ctx_);
        output_ctx_ = nullptr;
    }
    if (input_ctx_) {
        avformat_close_input(&input_ctx_);
        input_ctx_ = nullptr;
    }
    video_stream_idx_ = -1;
}

FileStreamer::Stats FileStreamer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}