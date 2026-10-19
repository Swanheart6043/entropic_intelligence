#pragma once
#include <string>
#include <vector>
#include <functional>

/**
 * 帧抓拍器 - 从 RTSP 流抓取单帧图像
 * 特点：
 * - 按需连接，抓完即断
 * - 支持保存为文件或返回内存数据
 * - 独立于推流器，可单独使用
 */
class FrameCapture {
public:
    FrameCapture() = default;
    ~FrameCapture() = default;

    // 禁用拷贝
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;

    /**
     * 抓取一帧并保存为 JPEG 文件
     * @param rtsp_url RTSP 地址
     * @param output_path 输出文件路径
     * @param quality JPEG 质量 (1-100)
     * @param timeout_ms 超时时间（毫秒）
     * @return 是否成功
     */
    bool capture_to_file(const std::string& rtsp_url, const std::string& output_path, int quality = 90, int timeout_ms = 5000);

    /**
     * 抓取一帧并返回 JPEG 数据
     * @param rtsp_url RTSP 地址
     * @param jpeg_data 输出 JPEG 数据
     * @param quality JPEG 质量 (1-100)
     * @param timeout_ms 超时时间（毫秒）
     * @return 是否成功
     */
    bool capture_to_memory(const std::string& rtsp_url, std::vector<uint8_t>& jpeg_data, int quality = 90, int timeout_ms = 5000);

    /**
     * 抓取一帧并通过回调返回
     * @param rtsp_url RTSP 地址
     * @param callback 回调函数
     * @param quality JPEG 质量 (1-100)
     * @param timeout_ms 超时时间（毫秒）
     * @return 是否成功
     */
    bool capture_with_callback(const std::string& rtsp_url, std::function<void(const uint8_t*, size_t)> callback, int quality = 90, int timeout_ms = 5000);

    /**
     * 获取最后错误信息
     */
    std::string get_last_error() const { return last_error_; }

private:
    bool do_capture(const std::string& rtsp_url, const std::string* file_path, std::vector<uint8_t>* out_data, std::function<void(const uint8_t*, size_t)>* callback, int quality, int timeout_ms);
    std::string last_error_;
};