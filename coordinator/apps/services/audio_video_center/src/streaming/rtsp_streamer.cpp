#include <spdlog/spdlog.h>
#include <chrono>
#include "rtsp_streamer.hpp"

static int64_t get_time_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

RtspStreamer::~RtspStreamer() {
    stop();
}

bool RtspStreamer::start(const std::string& input_url, const std::string& output_url) {
    if (running_.load()) {
        SPDLOG_WARN("推流器已在运行");
        return false;
    }
    stop_requested_.store(false);
    running_.store(true);
    stream_thread_ = std::thread(&RtspStreamer::stream_thread_func, this, input_url, output_url);
    // 等待一小段时间确认启动成功
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return running_.load();
}

void RtspStreamer::stop() {
    if (!running_.load() && !stream_thread_.joinable()) return;
    SPDLOG_INFO("正在停止推流...");
    stop_requested_.store(true);
    if (stream_thread_.joinable()) stream_thread_.join();
    running_.store(false);
}

void RtspStreamer::stream_thread_func(const std::string& input_url, const std::string& output_url) {
    SPDLOG_INFO("推流线程启动");

    if (!open_input(input_url)) {
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
    int consecutive_read_errors = 0;
    int consecutive_write_errors = 0;
    bool got_keyframe = false;
    const int max_errors = 50;

    int64_t last_write_time = get_time_us();
    const int64_t write_timeout = 10000000;

    // 使用系统时间生成时间戳
    std::chrono::steady_clock::time_point stream_start_time;
    bool time_initialized = false;

    while (!stop_requested_.load()) {
        if (got_keyframe && (get_time_us() - last_write_time > write_timeout)) {
            SPDLOG_ERROR("推流超时: 10秒内无数据写入");
            break;
        }
        int ret = av_read_frame(input_ctx_, packet);
        
        if (stop_requested_.load()) {
            av_packet_unref(packet);
            break;
        }   
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                SPDLOG_INFO("流结束");
                break;
            }
            if (ret == AVERROR(EAGAIN)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            consecutive_read_errors++;
            if (consecutive_read_errors > max_errors) {
                char err_buf[256];
                av_strerror(ret, err_buf, sizeof(err_buf));
                SPDLOG_ERROR("读取错误过多: {}", err_buf);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        consecutive_read_errors = 0;

        if (packet->stream_index != video_stream_idx_) {
            av_packet_unref(packet);
            continue;
        }
        if (!got_keyframe) {
            if (packet->flags & AV_PKT_FLAG_KEY) {
                got_keyframe = true;
                SPDLOG_INFO("收到关键帧，开始推流");
            } else {
                av_packet_unref(packet);
                continue;
            }
        }
        if (stop_requested_.load()) {
            av_packet_unref(packet);
            break;
        }
        // 使用系统时间生成时间戳
        auto now = std::chrono::steady_clock::now();
        if (!time_initialized) {
            stream_start_time = now;
            time_initialized = true;
            SPDLOG_INFO("推流时间基准已初始化");
        }
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - stream_start_time).count();
        AVStream* out_stream = output_ctx_->streams[0];
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
            
            bool fatal_error = (
                ret == AVERROR(EPIPE) ||
                ret == AVERROR_EOF ||
                ret == AVERROR(ECONNRESET) ||
                ret == AVERROR(ENOTCONN) ||
                ret == AVERROR(ENETUNREACH) ||
                ret == AVERROR(EHOSTUNREACH)
            );
            
            if (fatal_error) {
                SPDLOG_WARN("连接断开: {}", err_buf);
                av_packet_unref(packet);
                break;
            }
            
            consecutive_write_errors++;
            SPDLOG_WARN("写入帧失败: {}, 连续错误: {}", err_buf, consecutive_write_errors);

            if (consecutive_write_errors > max_errors) {
                SPDLOG_ERROR("写入错误过多: {}", err_buf);
                av_packet_unref(packet);
                break;
            }
            
            av_packet_unref(packet);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        consecutive_write_errors = 0;
        last_write_time = get_time_us();
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
    SPDLOG_INFO("推流线程结束，共处理 {} 帧", frame_count);
}

bool RtspStreamer::open_input(const std::string& url) {
    input_ctx_ = avformat_alloc_context();
    
    // 设置中断回调
    input_ctx_->interrupt_callback.callback = interrupt_callback;
    input_ctx_->interrupt_callback.opaque = this;

    AVDictionary* opts = nullptr;
    
    // ========== 低延迟核心选项 ==========
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "fflags", "nobuffer", 0); // 不缓冲
    av_dict_set(&opts, "flags", "low_delay", 0); // 低延迟标志
    av_dict_set(&opts, "analyzeduration", "2000000", 0); // 分析时长
    av_dict_set(&opts, "probesize", "2000000", 0); // 探测大小
    av_dict_set(&opts, "stimeout", "5000000", 0); // 连接超时 5秒
    av_dict_set(&opts, "buffer_size", "425984", 0); // 缓冲区大小
    av_dict_set(&opts, "max_delay", "300000", 0); // 最大延迟 300ms
    av_dict_set(&opts, "reorder_queue_size", "1000", 0); // 重排序队列

    int ret = avformat_open_input(&input_ctx_, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("打开输入失败: {}", err_buf);
        return false;
    }

    // 快速获取流信息
    input_ctx_->max_analyze_duration = 100000;
    
    ret = avformat_find_stream_info(input_ctx_, nullptr);
    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("获取流信息失败: {}", err_buf);
        return false;
    }

    // 查找视频流
    for (unsigned i = 0; i < input_ctx_->nb_streams; i++) {
        if (input_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx_ = i;
            auto* codecpar = input_ctx_->streams[i]->codecpar;

            // 如果还是 0x0，直接报错（不解码）
            if (codecpar->width == 0 || codecpar->height == 0) {
                SPDLOG_ERROR("无法获取视频尺寸，摄像头可能故障或配置错误");
                return false;
            }

            SPDLOG_INFO(
                "视频流: {}x{}, codec={}", 
                codecpar->width, 
                codecpar->height, 
                avcodec_get_name(codecpar->codec_id)
            );
            break;
        }
    }

    if (video_stream_idx_ < 0) {
        SPDLOG_ERROR("未找到视频流");
        return false;
    }

    SPDLOG_INFO("输入已打开: {}", url);
    return true;
}

bool RtspStreamer::open_output(const std::string& url) {
    int ret = avformat_alloc_output_context2(&output_ctx_, nullptr, "rtsp", url.c_str());
    if (ret < 0 || !output_ctx_) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        SPDLOG_ERROR("创建输出上下文失败: ", err_buf);
        return false;
    }

    // 只复制视频流
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

    // 低延迟输出选项
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "udp", 0);
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

void RtspStreamer::cleanup() {
    if (output_ctx_) {
        // 设置超时，避免阻塞, 刷新缓冲区，但不等待
        if (output_ctx_->pb) avio_flush(output_ctx_->pb);
        // 尝试写入 trailer，但限制时间
        if (output_ctx_->pb || !(output_ctx_->oformat->flags & AVFMT_NOFILE)) {
            int ret = av_write_trailer(output_ctx_);
            if (ret < 0) {
                char err_buf[256];
                av_strerror(ret, err_buf, sizeof(err_buf));
                SPDLOG_WARN("写入trailer失败: {}", err_buf);
            }
        }
        // 关闭IO
        if (output_ctx_->pb && !(output_ctx_->oformat->flags & AVFMT_NOFILE)) avio_closep(&output_ctx_->pb);
        avformat_free_context(output_ctx_);
        output_ctx_ = nullptr;
    }
    if (input_ctx_) {
        avformat_close_input(&input_ctx_);
        input_ctx_ = nullptr;
    }
    video_stream_idx_ = -1;
}

RtspStreamer::Stats RtspStreamer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}