#include <iostream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "bootstrap.hpp"
#include "config_parser.hpp"
#include "screenshot/capture_service.hpp"
#include "streaming/stream_service.hpp"
#include "recording/save_service.hpp"
#include "recording/query_service.hpp"
#include "recording/stream_service.hpp"

int main() {
    try {
        patrol_robot::init_crash_handler("audio_video_center");
        patrol_robot::init_logging("audio_video_center");
        auto config = Config::getInstance();
        config->loadFromFile("/etc/patrol_robot_h1/config.ini");

        CaptureService capture_service;
        RtspStreamingService rtsp_streaming_service;
        RecordingService recording_service;
        RecordingQueryService recording_query_service;
        RecordingStreamingService recording_streaming_service;

        capture_service.start();
        rtsp_streaming_service.start();
        recording_service.start();
        recording_streaming_service.start();
        recording_query_service.start();

        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("音视频中心异常: {}", e.what());
        return -1;
    }
}