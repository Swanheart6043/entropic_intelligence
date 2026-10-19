#include "mode_query_server.hpp"
#include <chrono>
#include <string>
#include <utility>
#include <spdlog/spdlog.h>

namespace {
    constexpr int kReceiveTimeoutMs = 200;
}

ModeQueryServer::ModeQueryServer(zmq::context_t& context, ModeProvider mode_provider) : socket_(context, zmq::socket_type::rep), mode_provider_(std::move(mode_provider)) {
    socket_.setsockopt(ZMQ_RCVTIMEO, &kReceiveTimeoutMs, sizeof(kReceiveTimeoutMs));
    socket_.bind("tcp://*:5555");
}

ModeQueryServer::~ModeQueryServer() {
    stop();
}

void ModeQueryServer::start() {
    if (running_.exchange(true)) {
        SPDLOG_WARN("Mode查询服务已经在运行");
        return;
    }
    try {
        worker_ = std::thread(&ModeQueryServer::message_loop, this);
    } catch (...) {
        running_ = false;
        throw;
    }
    SPDLOG_INFO("Mode查询服务已启动 (tcp://*:5555)");
}

void ModeQueryServer::stop() {
    const bool was_running = running_.exchange(false);
    if (worker_.joinable()) {
        worker_.join();
    }
    if (was_running) {
        SPDLOG_INFO("Mode查询服务已停止");
    }
}

void ModeQueryServer::message_loop() {
    while (running_.load()) {
        try {
            zmq::message_t request;
            const auto result = socket_.recv(request, zmq::recv_flags::none);

            if (!running_.load()) break;
            if (!result) continue;

            const std::string mode = mode_provider_() == RobotMode::AUTO ? "auto" : "manual";
            socket_.send(zmq::buffer(mode), zmq::send_flags::none);
            SPDLOG_DEBUG("响应mode查询: {}", mode);
        } catch (const zmq::error_t& e) {
            if (running_.load()) {
                SPDLOG_ERROR("Mode查询服务异常: {}", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}
