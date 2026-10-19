#pragma once
#include <map>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include "./recorder/rtsp_recorder.hpp"
#include "./recorder/disk_monitor.hpp"

class RecordingService {
public:
    RecordingService();
    ~RecordingService();
    void start();
    void stop();
    bool is_running() const;
    
private:
    struct CameraConfig {
        std::string name;
        std::string ip;
        std::string port;
        std::string path;
    };

    std::vector<CameraConfig> cameras = {
        {"high_definition", "10.10.10.40", "554", "/id=1&type=0"},
        {"infrared", "10.10.10.41", "554", "/cam/realmonitor?channel=1&subtype=0"},
        {"front", "10.10.10.42", "554", "/id=1&type=0"},
        {"rear", "10.10.10.43", "554", "/id=1&type=0"},
        {"left", "10.10.10.44", "554", "/id=1&type=0"},
        {"right", "10.10.10.45", "554", "/id=1&type=0"}
    };

    // 每个摄像头的上下文：自己的 recorder + 自己的线程
    struct CameraContext {
        RtspRecorder::Config config;
        RtspRecorder recorder; // request_stop() 是线程安全的
        std::thread thread;
    };

    std::map<std::string, std::unique_ptr<CameraContext>> camera_contexts_;
    DiskMonitor disk_monitor;
    std::atomic<bool> running{false};

    static constexpr int max_retry_delay_sec = 60;
    static constexpr int base_retry_delay_sec = 5;

    void camera_loop(CameraContext& ctx, const std::string& name);
    int get_retry_delay(int fail_count) const;
    RtspRecorder::Config make_recorder_config(const CameraConfig& cam) const;
};