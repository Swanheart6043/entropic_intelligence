#include "frame_capture.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

bool FrameCapture::capture_to_file(const std::string& rtsp_url, const std::string& output_path, int quality, int timeout_ms) {
    return do_capture(rtsp_url, &output_path, nullptr, nullptr, quality, timeout_ms);
}

bool FrameCapture::capture_to_memory(const std::string& rtsp_url, std::vector<uint8_t>& jpeg_data, int quality, int timeout_ms) {
    return do_capture(rtsp_url, nullptr, &jpeg_data, nullptr, quality, timeout_ms);
}

bool FrameCapture::capture_with_callback(const std::string& rtsp_url, std::function<void(const uint8_t*, size_t)> callback, int quality, int timeout_ms) {
    return do_capture(rtsp_url, nullptr, nullptr, &callback, quality, timeout_ms);
}

bool FrameCapture::do_capture(const std::string& rtsp_url, const std::string* file_path, std::vector<uint8_t>* out_data, std::function<void(const uint8_t*, size_t)>* callback, int quality, int timeout_ms) {
    AVFormatContext* input_ctx = nullptr;
    AVCodecContext* decoder_ctx = nullptr;
    AVCodecContext* encoder_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* converted_frame = nullptr;
    bool success = false;

    auto cleanup = [&]() {
        if (converted_frame) av_frame_free(&converted_frame);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (sws_ctx) sws_freeContext(sws_ctx);
        if (encoder_ctx) avcodec_free_context(&encoder_ctx);
        if (decoder_ctx) avcodec_free_context(&decoder_ctx);
        if (input_ctx) avformat_close_input(&input_ctx);
    };

    // 1. 打开输入
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);  // 抓图用 TCP 更稳定
    av_dict_set(&opts, "stimeout", std::to_string(timeout_ms * 1000).c_str(), 0);
    av_dict_set(&opts, "analyzeduration", "500000", 0);
    av_dict_set(&opts, "probesize", "500000", 0);

    int ret = avformat_open_input(&input_ctx, rtsp_url.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char err_buf[256];
        av_strerror(ret, err_buf, sizeof(err_buf));
        last_error_ = std::string("打开RTSP失败: ") + err_buf;
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    ret = avformat_find_stream_info(input_ctx, nullptr);
    if (ret < 0) {
        last_error_ = "获取流信息失败";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    // 2. 查找视频流
    int video_idx = -1;
    for (unsigned i = 0; i < input_ctx->nb_streams; i++) {
        if (input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_idx = i;
            break;
        }
    }

    if (video_idx < 0) {
        last_error_ = "未找到视频流";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    // 3. 初始化解码器
    AVCodecParameters* codecpar = input_ctx->streams[video_idx]->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(codecpar->codec_id);
    if (!decoder) {
        last_error_ = "未找到解码器";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    decoder_ctx = avcodec_alloc_context3(decoder);
    avcodec_parameters_to_context(decoder_ctx, codecpar);
    decoder_ctx->thread_count = 2;

    ret = avcodec_open2(decoder_ctx, decoder, nullptr);
    if (ret < 0) {
        last_error_ = "打开解码器失败";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    // 4. 初始化 JPEG 编码器
    const AVCodec* jpeg_encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!jpeg_encoder) {
        last_error_ = "未找到JPEG编码器";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    encoder_ctx = avcodec_alloc_context3(jpeg_encoder);
    encoder_ctx->width = codecpar->width;
    encoder_ctx->height = codecpar->height;
    encoder_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    encoder_ctx->time_base = {1, 25};
    encoder_ctx->qmin = 2;
    encoder_ctx->qmax = 2 + (100 - quality) * 29 / 100;

    ret = avcodec_open2(encoder_ctx, jpeg_encoder, nullptr);
    if (ret < 0) {
        last_error_ = "打开JPEG编码器失败";
        SPDLOG_ERROR("{}", last_error_);
        cleanup();
        return false;
    }

    // 5. 分配帧和包
    packet = av_packet_alloc();
    frame = av_frame_alloc();
    converted_frame = av_frame_alloc();

    converted_frame->width = codecpar->width;
    converted_frame->height = codecpar->height;
    converted_frame->format = AV_PIX_FMT_YUVJ420P;
    av_frame_get_buffer(converted_frame, 0);

    // 6. 读取并解码，找到关键帧
    auto start = std::chrono::steady_clock::now();
    bool got_frame = false;

    while (!got_frame) {
        // 检查超时
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeout_ms) {
            last_error_ = "抓图超时";
            SPDLOG_ERROR("{}", last_error_);
            cleanup();
            return false;
        }

        ret = av_read_frame(input_ctx, packet);
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) continue;
            char err_buf[256];
            av_strerror(ret, err_buf, sizeof(err_buf));
            last_error_ = std::string("读取帧失败: ") + err_buf;
            SPDLOG_ERROR("{}", last_error_);
            cleanup();
            return false;
        }

        if (packet->stream_index != video_idx) {
            av_packet_unref(packet);
            continue;
        }

        // 解码
        ret = avcodec_send_packet(decoder_ctx, packet);
        if (ret < 0) {
            av_packet_unref(packet);
            continue;
        }

        ret = avcodec_receive_frame(decoder_ctx, frame);
        if (ret >= 0) {
            got_frame = true;
        }

        av_packet_unref(packet);
    }

    SPDLOG_INFO("成功解码帧: {}x{}, format={}", frame->width, frame->height, frame->format);

    // 7. 像素格式转换（如果需要）
    AVFrame* encode_frame = frame;
    if (frame->format != AV_PIX_FMT_YUVJ420P && frame->format != AV_PIX_FMT_YUV420P) {
        sws_ctx = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height, AV_PIX_FMT_YUVJ420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

        if (!sws_ctx) {
            last_error_ = "创建格式转换上下文失败";
            SPDLOG_ERROR("{}", last_error_);
            cleanup();
            return false;
        }

        sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, converted_frame->data, converted_frame->linesize);
        encode_frame = converted_frame;
    }

    // 8. 编码为 JPEG
    AVPacket* jpeg_pkt = av_packet_alloc();
    
    ret = avcodec_send_frame(encoder_ctx, encode_frame);
    if (ret < 0) {
        last_error_ = "发送帧到编码器失败";
        SPDLOG_ERROR("{}", last_error_);
        av_packet_free(&jpeg_pkt);
        cleanup();
        return false;
    }

    ret = avcodec_receive_packet(encoder_ctx, jpeg_pkt);
    if (ret < 0) {
        last_error_ = "获取JPEG数据失败";
        SPDLOG_ERROR("{}", last_error_);
        av_packet_free(&jpeg_pkt);
        cleanup();
        return false;
    }

    // 9. 输出
    if (file_path) {
        std::ofstream file(*file_path, std::ios::binary);
        if (file) {
            file.write((const char*)jpeg_pkt->data, jpeg_pkt->size);
            file.close();
            SPDLOG_INFO("抓图保存到: {} ({} bytes)", *file_path, jpeg_pkt->size);
            success = true;
        } else {
            last_error_ = "打开输出文件失败: " + *file_path;
            SPDLOG_ERROR("{}", last_error_);
        }
    }

    if (out_data) {
        out_data->assign(jpeg_pkt->data, jpeg_pkt->data + jpeg_pkt->size);
        SPDLOG_INFO("抓图到内存: {} bytes", jpeg_pkt->size);
        success = true;
    }

    if (callback && *callback) {
        (*callback)(jpeg_pkt->data, jpeg_pkt->size);
        SPDLOG_INFO("抓图回调: {} bytes", jpeg_pkt->size);
        success = true;
    }

    av_packet_free(&jpeg_pkt);
    cleanup();
    return success;
}