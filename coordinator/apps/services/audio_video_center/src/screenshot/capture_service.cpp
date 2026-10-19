#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "capture_service.hpp"
#include "./frame_capture.hpp"

CaptureService::CaptureService() {
    socket.bind("ipc://@audio_video_req");
}

CaptureService::~CaptureService() {
    stop();
}

void CaptureService::start() {
    if (running) {
        SPDLOG_WARN("抓图线程已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&CaptureService::message_loop, this);
    SPDLOG_INFO("抓图线程启动");
}

void CaptureService::stop() {
    
}

void CaptureService::message_loop() {
    while (true) {
        std::vector<uint8_t> image;
        try {
            zmq::message_t zmq_message;
            auto recv_result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!recv_result) continue;
            
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            nlohmann::json message = nlohmann::json::parse(message_string);
            SPDLOG_INFO("抓图线程收到消息: {}", message_string);
            
            if (message.at("type") == "screenshot") {
                FrameCapture frame_capture;
                frame_capture.capture_to_memory("rtsp://admin:heyn_888@10.10.10.40:554/id=1&type=0", image, 90, 5);         
            }
            socket.send(zmq::buffer(image), zmq::send_flags::none);

        } catch (const std::exception& e) {
            SPDLOG_ERROR("抓图线程异常: {}", e.what());
            socket.send(zmq::buffer(image), zmq::send_flags::none);
        }
    }
}
