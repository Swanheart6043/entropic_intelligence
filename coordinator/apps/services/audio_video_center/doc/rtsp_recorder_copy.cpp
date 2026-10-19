#include <spdlog/spdlog.h>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/statvfs.h>
#include "rtsp_recorder.hpp"

RtspRecorder::RtspRecorder() {
}

RtspRecorder::~RtspRecorder() {
    cleanup();
}

bool RtspRecorder::run(const Config& config) {
    config_ = config;
    stop_requested_.store(false);

    try {
        std::filesystem::create_directories(config_.output_dir);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("[{}] 创建输出目录失败: {}", config_.file_prefix, e.what());
        return false;
    }

    if (should_stop_recording()) {
        SPDLOG_ERROR("[{}] 磁盘空间不足，无法启动录像", config_.file_prefix);
        return false;
    }

    if (!open_input()) {
        cleanup();
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    bool got_keyframe = false;
    int consecutive_errors = 0;
    const int max_errors = 100;
    int frame_count = 0;
    const int check_interval = 100;
    bool ever_recorded = false;  // 是否曾经成功录像过
    
    // ===== 阶段1: 寻找第一个关键帧 =====
    SPDLOG_INFO("[{}] 正在寻找关键帧...", config_.file_prefix);
    while (!stop_requested_.load()) {
        int ret = av_read_frame(input_ctx_, packet);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            consecutive_errors++;
            if (consecutive_errors > max_errors) {
                SPDLOG_ERROR("[{}] 读取错误过多", config_.file_prefix);
                av_packet_free(&packet);
                cleanup();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (packet->stream_index != video_stream_idx_) {
            av_packet_unref(packet);
            continue;
        }

        if (packet->flags & AV_PKT_FLAG_KEY) {
            got_keyframe = true;
            SPDLOG_INFO("[{}] 找到关键帧", config_.file_prefix);
            av_packet_unref(packet);
            break;
        }

        av_packet_unref(packet);
    }

    if (!got_keyframe) {
        last_error_ = "未找到关键帧（流可能不正常）";
        SPDLOG_DEBUG("[{}] 未找到关键帧", config_.file_prefix);
        av_packet_free(&packet);
        cleanup();
        return false;
    }

    avformat_flush(input_ctx_);

    // ===== 阶段2: 打开第一个输出文件 =====
    std::string current_file = generate_filename();
    if (!open_output(current_file)) {
        cleanup();
        return false;
    }

    ever_recorded = true;
    last_error_.clear();
    SPDLOG_INFO("[{}] 录像已就绪", config_.file_prefix);

    segment_start_time_ = std::chrono::steady_clock::now();
    got_keyframe = false;
    consecutive_errors = 0;

    // ===== 阶段3: 持续录像 =====
    while (!stop_requested_.load()) {
        if (frame_count % check_interval == 0) {
            if (should_stop_recording()) {
                SPDLOG_ERROR("[{}] 磁盘空间不足，停止录像", config_.file_prefix);
                break;
            }
        }

        int ret = av_read_frame(input_ctx_, packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SPDLOG_INFO("[{}] 流结束", config_.file_prefix);
                break;
            }
            if (ret == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            consecutive_errors++;
            if (consecutive_errors > max_errors) {
                SPDLOG_ERROR("[{}] 读取错误过多，停止录像", config_.file_prefix);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        consecutive_errors = 0;

        if (packet->stream_index != video_stream_idx_) {
            av_packet_unref(packet);
            continue;
        }

        if (!got_keyframe) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                got_keyframe = true;
                SPDLOG_INFO("[{}] 收到关键帧，开始录像", config_.file_prefix);
            } else {
                av_packet_unref(packet);
                continue;
            }
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(
            now - segment_start_time_).count();

        if (elapsed_sec >= config_.segment_duration_sec && (packet->flags & AV_PKT_FLAG_KEY)) {
            SPDLOG_INFO("[{}] 达到分段时长 {}秒，切换文件", config_.file_prefix, elapsed_sec);

            close_output();
            remux_to_standard_mp4(current_file);
            avformat_flush(input_ctx_);

            current_file = generate_filename();
            if (!open_output(current_file)) {
                SPDLOG_ERROR("[{}] 打开新输出文件失败", config_.file_prefix);
                av_packet_unref(packet);
                break;
            }

            segment_start_time_ = std::chrono::steady_clock::now();
            av_packet_unref(packet);
            got_keyframe = false;
            continue;
        }

        AVStream* out_stream = output_ctx_->streams[0];
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now - segment_start_time_).count();

        packet->pts = av_rescale_q(elapsed_us, {1, 1000000}, out_stream->time_base);
        packet->dts = packet->pts;
        packet->duration = 0;
        packet->stream_index = 0;
        packet->pos = -1;

        ret = av_interleaved_write_frame(output_ctx_, packet);
        if (ret < 0) {
            char err[256];
            av_strerror(ret, err, sizeof(err));
            SPDLOG_WARN("[{}] 写入帧失败: {}", config_.file_prefix, err);
        } else {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.total_bytes += packet->size;
            stats_.current_file_bytes += packet->size;
            frame_count++;
        }

        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    close_output();
    remux_to_standard_mp4(current_file);
    close_input();

    SPDLOG_INFO("[{}] 录像结束", config_.file_prefix);
    return ever_recorded;
}

void RtspRecorder::request_stop() {
    stop_requested_.store(true);
}

bool RtspRecorder::should_stop_recording() {
    auto disk = get_disk_info(config_.disk_mount_point);
    if (disk.used_percent >= config_.disk_critical_percent) {
        SPDLOG_ERROR(
            "[{}] 磁盘空间严重不足 {}% (临界值 {}%), 停止录像! 可用: {}MB / {}MB",
            config_.file_prefix,
            disk.used_percent,
            config_.disk_critical_percent,
            disk.available_mb,
            disk.total_mb
        );
        return true;
    }
    return false;
}

bool RtspRecorder::open_input() {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "buffer_size", "1048576", 0);

    int ret = avformat_open_input(&input_ctx_, config_.rtsp_url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        char err[256];
        av_strerror(ret, err, sizeof(err));
        last_error_ = err;
        SPDLOG_DEBUG("[{}] 打开RTSP失败: {}", config_.file_prefix, err);
        return false;
    }

    ret = avformat_find_stream_info(input_ctx_, nullptr);
    if (ret < 0) {
        last_error_ = "获取流信息失败";
        SPDLOG_DEBUG("[{}] 获取流信息失败", config_.file_prefix);
        return false;
    }
    for (unsigned i = 0; i < input_ctx_->nb_streams; i++) {
        if (input_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx_ = i;
            break;
        }
    }
    if (video_stream_idx_ < 0) {
        last_error_ = "未找到视频流";
        SPDLOG_DEBUG("[{}] 未找到视频流", config_.file_prefix);
        return false;
    }
    SPDLOG_INFO("[{}] 输入流已打开", config_.file_prefix);
    return true;
}

std::string RtspRecorder::generate_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_r(&time_t, &tm);

    std::ostringstream oss;
    oss << config_.output_dir << "/"
        << config_.file_prefix << "_"
        << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";

    return oss.str();
}

bool RtspRecorder::open_output(const std::string& filename) {
    int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, nullptr, filename.c_str());
    if (ret < 0 || !output_ctx_) {
        SPDLOG_ERROR("[{}] 创建输出上下文失败", config_.file_prefix);
        return false;
    }

    AVStream* in_stream = input_ctx_->streams[video_stream_idx_];
    AVStream* out_stream = avformat_new_stream(output_ctx_, nullptr);
    if (!out_stream) {
        SPDLOG_ERROR("[{}] 创建输出流失败", config_.file_prefix);
        return false;
    }

    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        SPDLOG_ERROR("[{}] 复制编码参数失败", config_.file_prefix);
        return false;
    }
    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&output_ctx_->pb, filename.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            SPDLOG_ERROR("[{}] 打开输出文件失败: {}", config_.file_prefix, filename);
            return false;
        }
    }

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov", 0);
    ret = avformat_write_header(output_ctx_, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        SPDLOG_ERROR("[{}] 写入文件头失败", config_.file_prefix);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.current_file = filename;
        stats_.current_file_bytes = 0;
        stats_.files_created++;
    }

    SPDLOG_INFO("[{}] 开始录制新文件: {}", config_.file_prefix, filename);
    return true;
}

void RtspRecorder::close_input() {
    if (input_ctx_) {
        avformat_close_input(&input_ctx_);
        input_ctx_ = nullptr;
    }
}

void RtspRecorder::close_output() {
    if (!output_ctx_) return;

    av_write_trailer(output_ctx_);

    if (!(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx_->pb);
    }

    avformat_free_context(output_ctx_);
    output_ctx_ = nullptr;

    SPDLOG_INFO("[{}] 文件录制完成", config_.file_prefix);
}

RtspRecorder::DiskInfo RtspRecorder::get_disk_info(const std::string& path) {
    DiskInfo info;

    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        SPDLOG_ERROR("获取磁盘信息失败: {}", path);
        return info;
    }

    uint64_t total_bytes = stat.f_blocks * stat.f_frsize;
    uint64_t available_bytes = stat.f_bavail * stat.f_frsize;
    uint64_t used_bytes = total_bytes - available_bytes;

    info.total_mb = total_bytes / 1024 / 1024;
    info.available_mb = available_bytes / 1024 / 1024;
    info.used_percent = (total_bytes > 0) ? (used_bytes * 100) / total_bytes : 0;

    return info;
}

void RtspRecorder::cleanup() {
    close_output();
    close_input();
}

void RtspRecorder::remux_to_standard_mp4(const std::string& frag_file) {
    std::string temp_file = frag_file + ".tmp";

    try {
        std::filesystem::rename(frag_file, temp_file);
    } catch (const std::exception& e) {
        SPDLOG_WARN("[{}] remux重命名失败: {}", config_.file_prefix, e.what());
        return;
    }

    AVFormatContext* in_ctx = nullptr;
    AVFormatContext* out_ctx = nullptr;
    bool success = false;

    do {
        if (avformat_open_input(&in_ctx, temp_file.c_str(), nullptr, nullptr) < 0) break;
        if (avformat_find_stream_info(in_ctx, nullptr) < 0) break;
        if (avformat_alloc_output_context2(&out_ctx, nullptr, nullptr, frag_file.c_str()) < 0) break;

        bool streams_ok = true;
        for (unsigned i = 0; i < in_ctx->nb_streams; i++) {
            AVStream* out_stream = avformat_new_stream(out_ctx, nullptr);
            if (!out_stream) { streams_ok = false; break; }
            if (avcodec_parameters_copy(out_stream->codecpar, in_ctx->streams[i]->codecpar) < 0) {
                streams_ok = false; break;
            }
            out_stream->codecpar->codec_tag = 0;
        }
        if (!streams_ok) break;

        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&out_ctx->pb, frag_file.c_str(), AVIO_FLAG_WRITE) < 0) break;
        }

        if (avformat_write_header(out_ctx, nullptr) < 0) break;

        AVPacket* pkt = av_packet_alloc();
        bool write_ok = true;
        while (av_read_frame(in_ctx, pkt) >= 0) {
            pkt->stream_index = 0;
            if (av_interleaved_write_frame(out_ctx, pkt) < 0) {
                write_ok = false;
                break;
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        if (!write_ok) break;

        av_write_trailer(out_ctx);
        success = true;
    } while (false);

    if (in_ctx) avformat_close_input(&in_ctx);
    if (out_ctx) {
        if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_ctx->pb);
        avformat_free_context(out_ctx);
    }

    if (success) {
        std::filesystem::remove(temp_file);
        SPDLOG_INFO("[{}] remux完成: {}", config_.file_prefix, frag_file);
    } else {
        SPDLOG_WARN("[{}] remux失败，保留frag版本", config_.file_prefix);
        std::filesystem::remove(frag_file);
        std::filesystem::rename(temp_file, frag_file);
    }
}

std::string RtspRecorder::get_last_error() const {
    return last_error_;
}
