#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
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

    // 【本次改动】pacing 改为按"源 PTS"统一节流，音视频共用同一条时间轴 → 音画同步。
    // 基准 base_pts_us 取第一个被写出的包(必为视频关键帧)的源 PTS(统一换算到微秒)。
    std::chrono::steady_clock::time_point wall_start;
    int64_t base_pts_us = AV_NOPTS_VALUE;
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

        // 【本次改动】查表决定这一路流是否推流（视频/音频推，其它丢弃）
        int map_idx = (packet->stream_index < (int)in_to_map_.size())
                      ? in_to_map_[packet->stream_index] : -1;
        if (map_idx < 0) {
            av_packet_unref(packet);
            continue;
        }
        bool is_video = (packet->stream_index == video_stream_idx_);

        // 等第一个视频关键帧再开始；关键帧之前的音频包丢弃，避免开头有声无画
        if (!got_keyframe) {
            if (is_video && (packet->flags & AV_PKT_FLAG_KEY)) {
                got_keyframe = true;
                SPDLOG_INFO("收到关键帧，开始推流");
            } else {
                av_packet_unref(packet);
                continue;
            }
        }

        StreamMapping& m = stream_maps_[map_idx];

        // 该包的源时间戳换算到微秒（无 pts 回退到 dts）
        int64_t src_ts = (packet->pts != AV_NOPTS_VALUE) ? packet->pts : packet->dts;
        int64_t src_pts_us = (src_ts != AV_NOPTS_VALUE)
                             ? av_rescale_q(src_ts, m.in_tb, {1, 1000000})
                             : AV_NOPTS_VALUE;

        // ====== 速率控制：按源 PTS 统一 pacing ======
        if (src_pts_us != AV_NOPTS_VALUE) {
            if (!pacing_initialized) {
                base_pts_us = src_pts_us;
                wall_start = std::chrono::steady_clock::now();
                pacing_initialized = true;
            }

            int64_t target_elapsed_us = src_pts_us - base_pts_us;
            if (target_elapsed_us < 0) target_elapsed_us = 0;

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

        // ====== 输出时间戳：源 PTS 相对基准 + rescale + 单调钳制 ======
        // 【本次改动】不再用 wall_start 重算时间戳(那样会丢掉音视频的相对关系导致不同步)。
        // 改为各流基于源 pts/dts 减去公共基准，再钳制成单调递增。
        int64_t rel_pts = 0, rel_dts = 0;
        if (base_pts_us != AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE) {
            rel_pts = av_rescale_q(packet->pts, m.in_tb, {1, 1000000}) - base_pts_us;
        }
        if (base_pts_us != AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE) {
            rel_dts = av_rescale_q(packet->dts, m.in_tb, {1, 1000000}) - base_pts_us;
        } else {
            rel_dts = rel_pts;
        }
        if (rel_pts < 0) rel_pts = 0;
        if (rel_dts < 0) rel_dts = 0;

        int64_t out_pts = av_rescale_q(rel_pts, {1, 1000000}, m.out_tb);
        int64_t out_dts = av_rescale_q(rel_dts, {1, 1000000}, m.out_tb);

        // 单调钳制：消除 pts/dts 倒流/停滞导致的写帧失败刷屏
        if (m.last_out_dts != AV_NOPTS_VALUE && out_dts <= m.last_out_dts) {
            int64_t delta = m.last_out_dts + 1 - out_dts;
            out_dts += delta;
            out_pts += delta;
        }
        if (out_pts < out_dts) out_pts = out_dts;  // 兜底
        m.last_out_dts = out_dts;

        packet->pts = out_pts;
        packet->dts = out_dts;
        packet->duration = av_rescale_q(packet->duration, m.in_tb, m.out_tb);
        packet->pos = -1;
        packet->stream_index = m.out_idx;  // 映射到输出流索引

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

    // 【本次改动】收集第一路视频 + 所有音频。无音频文件 stream_maps_ 只有视频一路，
    // 行为与改造前等价。其它类型(DATA/SUBTITLE 等)丢弃；画面上的 OSD 文字在视频像素里，不受影响。
    in_to_map_.assign(input_ctx_->nb_streams, -1);
    stream_maps_.clear();
    video_stream_idx_ = -1;

    for (unsigned i = 0; i < input_ctx_->nb_streams; i++) {
        auto type = input_ctx_->streams[i]->codecpar->codec_type;

        bool take = false;
        if (type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ < 0) {
            video_stream_idx_ = (int)i;
            take = true;
            auto* cp = input_ctx_->streams[i]->codecpar;
            SPDLOG_INFO("视频流: {}x{}, codec={}", cp->width, cp->height,
                        avcodec_get_name(cp->codec_id));
        } else if (type == AVMEDIA_TYPE_AUDIO) {
            take = true;
            SPDLOG_INFO("音频流: codec={}",
                        avcodec_get_name(input_ctx_->streams[i]->codecpar->codec_id));
        }
        if (!take) continue;

        StreamMapping m;
        m.in_idx = (int)i;
        m.in_tb  = input_ctx_->streams[i]->time_base;
        in_to_map_[i] = (int)stream_maps_.size();
        stream_maps_.push_back(m);
    }

    if (video_stream_idx_ < 0) {
        SPDLOG_ERROR("未找到视频流");
        return false;
    }

    SPDLOG_INFO("文件已打开: {} ({}路流)", file_path, stream_maps_.size());
    return true;
}

bool FileStreamer::open_output(const std::string& url) {
    header_written_ = false;  // 【本次改动】新 context，未写头

    int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, "rtsp", url.c_str());
    if (ret < 0 || !output_ctx_) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("创建输出上下文失败: {}", err_buf);
        return false;
    }

    // 【本次改动】为每路被推流的流创建对应输出流
    for (auto& m : stream_maps_) {
        AVStream* in_stream  = input_ctx_->streams[m.in_idx];
        AVStream* out_stream = avformat_new_stream(output_ctx_, nullptr);
        if (!out_stream) {
            SPDLOG_ERROR("创建输出流失败");
            return false;
        }
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0) {
            SPDLOG_ERROR("复制编码参数失败");
            return false;
        }
        out_stream->codecpar->codec_tag = 0;
        out_stream->time_base = in_stream->time_base;
        m.out_idx = out_stream->index;
        m.last_out_dts = AV_NOPTS_VALUE;
    }

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
        return false;  // header_written_ 仍为 false
    }
    header_written_ = true;  // 只有到这里才允许写 trailer

    // 【本次改动】out_tb 必须在 write_header 之后读取（封装器可能改写 time_base）
    for (auto& m : stream_maps_) {
        m.out_tb = output_ctx_->streams[m.out_idx]->time_base;
    }

    SPDLOG_INFO("输出已打开: {}", url);
    return true;
}

void FileStreamer::cleanup() {
    if (output_ctx_) {
        if (output_ctx_->pb) {
            avio_flush(output_ctx_->pb);
        }
        // 【本次改动】只有 write_header 成功过才写 trailer，
        // 否则会对未初始化的 muxer 私有数据解引用导致段错误(录像端踩过同样的坑)。
        if (header_written_) {
            av_write_trailer(output_ctx_);
        }
        header_written_ = false;
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
    stream_maps_.clear();
    in_to_map_.clear();
}

FileStreamer::Stats FileStreamer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}