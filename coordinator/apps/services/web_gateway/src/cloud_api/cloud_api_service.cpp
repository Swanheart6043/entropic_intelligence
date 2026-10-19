#include "cloud_api_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <regex>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include "cloud_gateway_client.hpp"

using Json = nlohmann::json;

namespace {

std::int64_t unix_time_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

Json error_reply(
    const std::string& request_id,
    const std::string& code,
    const std::string& message,
    bool retryable,
    int upstream_status = 0
) {
    return {
        {"version", cloud_gateway::kProtocolVersion},
        {"request_id", request_id},
        {"ok", false},
        {"accepted", false},
        {"upstream_status", upstream_status},
        {"error", {
            {"code", code},
            {"message", message},
            {"retryable", retryable}
        }}
    };
}

Json success_reply(
    const std::string& request_id,
    const Json& result = Json::object(),
    bool accepted = false,
    int upstream_status = 0
) {
    return {
        {"version", cloud_gateway::kProtocolVersion},
        {"request_id", request_id},
        {"ok", true},
        {"accepted", accepted},
        {"upstream_status", upstream_status},
        {"result", result}
    };
}

void forward_multipart(zmq::socket_t& source, zmq::socket_t& destination) {
    while (true) {
        zmq::message_t part;
        if (!source.recv(part, zmq::recv_flags::none)) return;

        int more = 0;
        std::size_t more_size = sizeof(more);
        source.getsockopt(ZMQ_RCVMORE, &more, &more_size);
        destination.send(
            part,
            more ? zmq::send_flags::sndmore : zmq::send_flags::none
        );
        if (!more) return;
    }
}

std::vector<std::string> receive_frames(zmq::socket_t& socket) {
    std::vector<std::string> frames;
    while (true) {
        zmq::message_t part;
        if (!socket.recv(part, zmq::recv_flags::none)) return {};
        frames.emplace_back(static_cast<const char*>(part.data()), part.size());

        int more = 0;
        std::size_t more_size = sizeof(more);
        socket.getsockopt(ZMQ_RCVMORE, &more, &more_size);
        if (!more) return frames;
    }
}

bool safe_request_id(const std::string& request_id) {
    static const std::regex pattern("^[a-fA-F0-9-]{16,64}$");
    return std::regex_match(request_id, pattern);
}

} // namespace

CloudApiService::CloudApiService(WebSocketClient& websocket, std::size_t rpc_worker_count)
    : platform_client_(websocket),
      outbox_(config_->get_cloud_outbox_dir(), config_->get_cloud_outbox_policy()),
      rpc_worker_count_(std::max<std::size_t>(1, rpc_worker_count)) {}

CloudApiService::~CloudApiService() {
    stop();
}

void CloudApiService::start() {
    if (running_.exchange(true)) return;

    try {
        outbox_.start();
    } catch (...) {
        running_ = false;
        throw;
    }

    {
        std::lock_guard<std::mutex> lock(broker_ready_mutex_);
        broker_ready_ = false;
        broker_error_.clear();
    }
    broker_thread_ = std::thread(&CloudApiService::broker_loop, this);
    {
        std::unique_lock<std::mutex> lock(broker_ready_mutex_);
        broker_ready_cv_.wait(lock, [this] { return broker_ready_ || !broker_error_.empty(); });
        if (!broker_error_.empty()) {
            const std::string error = broker_error_;
            running_ = false;
            lock.unlock();
            if (broker_thread_.joinable()) broker_thread_.join();
            throw std::runtime_error(error);
        }
    }

    rpc_workers_.reserve(rpc_worker_count_);
    for (std::size_t index = 0; index < rpc_worker_count_; ++index) {
        rpc_workers_.emplace_back(&CloudApiService::rpc_worker_loop, this, index);
    }
    outbox_thread_ = std::thread(&CloudApiService::outbox_loop, this);
    SPDLOG_INFO("云端API网关已启动: rpc_workers={}, outbox={}", rpc_worker_count_, config_->get_cloud_outbox_dir());
}

void CloudApiService::stop() {
    if (!running_.exchange(false)) return;
    outbox_cv_.notify_all();

    for (auto& worker : rpc_workers_) {
        if (worker.joinable()) worker.join();
    }
    rpc_workers_.clear();
    if (outbox_thread_.joinable()) outbox_thread_.join();
    if (broker_thread_.joinable()) broker_thread_.join();
}

void CloudApiService::broker_loop() {
    try {
        zmq::socket_t frontend(context_, zmq::socket_type::router);
        zmq::socket_t backend(context_, zmq::socket_type::dealer);
        const int linger = 0;
        frontend.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        backend.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
        frontend.bind(cloud_gateway::kRpcEndpoint);
        backend.bind(kBackendEndpoint);

        {
            std::lock_guard<std::mutex> lock(broker_ready_mutex_);
            broker_ready_ = true;
        }
        broker_ready_cv_.notify_all();

        zmq::pollitem_t items[] = {
            {static_cast<void*>(frontend), 0, ZMQ_POLLIN, 0},
            {static_cast<void*>(backend), 0, ZMQ_POLLIN, 0}
        };
        while (running_) {
            zmq::poll(items, 2, std::chrono::milliseconds(100));
            if (items[0].revents & ZMQ_POLLIN) forward_multipart(frontend, backend);
            if (items[1].revents & ZMQ_POLLIN) forward_multipart(backend, frontend);
        }
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(broker_ready_mutex_);
            if (!broker_ready_) broker_error_ = std::string("cloud API broker failed: ") + e.what();
        }
        broker_ready_cv_.notify_all();
        SPDLOG_ERROR("云端API broker异常: {}", e.what());
    }
}

void CloudApiService::rpc_worker_loop(std::size_t worker_index) {
    while (running_) {
        try {
            zmq::socket_t socket(context_, zmq::socket_type::rep);
            const int receive_timeout_ms = 200;
            const int send_timeout_ms = 1000;
            const int linger = 0;
            socket.setsockopt(ZMQ_RCVTIMEO, &receive_timeout_ms, sizeof(receive_timeout_ms));
            socket.setsockopt(ZMQ_SNDTIMEO, &send_timeout_ms, sizeof(send_timeout_ms));
            socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
            socket.connect(kBackendEndpoint);

            while (running_) {
                const auto frames = receive_frames(socket);
                if (frames.empty()) continue;

                Json reply;
                try {
                    if (frames.size() > 2) {
                        throw std::invalid_argument("RPC request has too many frames");
                    }
                    const Json request = Json::parse(frames.front());
                    reply = handle_rpc(request, frames.size() == 2 ? frames[1] : std::string{});
                } catch (const std::exception& e) {
                    reply = error_reply("", "invalid_request", e.what(), false);
                }
                const std::string encoded = reply.dump();
                if (!socket.send(zmq::buffer(encoded), zmq::send_flags::none)) {
                    SPDLOG_WARN("云端API worker {} 回复超时，重建REP socket", worker_index);
                    break;
                }
            }
        } catch (const std::exception& e) {
            if (running_) SPDLOG_ERROR("云端API worker {} 异常: {}", worker_index, e.what());
        }
        if (running_) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

Json CloudApiService::handle_rpc(const Json& request, const std::string& binary_payload) {
    const std::string request_id = request.value("request_id", "");
    if (request.value("version", 0) != cloud_gateway::kProtocolVersion) {
        return error_reply(request_id, "protocol_mismatch", "unsupported protocol version", false);
    }
    if (!safe_request_id(request_id)) {
        return error_reply(request_id, "invalid_request_id", "request_id format is invalid", false);
    }
    if (!request.contains("payload") || !request.at("payload").is_object()) {
        return error_reply(request_id, "invalid_payload", "payload must be an object", false);
    }

    const std::string operation = request.value("operation", "");
    if (operation == "get_server_time") {
        const CloudPlatformResult result = platform_client_.get_server_time();
        if (!result.ok) {
            return error_reply(
                request_id,
                result.error_code,
                result.error_message,
                result.retryable,
                result.http_status
            );
        }
        return success_reply(request_id, result.result, false, result.http_status);
    }

    if (operation == "enqueue_alarm_report" || operation == "enqueue_capture_report") {
        if (binary_payload.empty()) {
            return error_reply(request_id, "missing_binary", "event report requires an image", false);
        }
        std::string error_code;
        std::string error_message;
        if (!outbox_.enqueue(request, binary_payload, error_code, error_message)) {
            const bool retryable = error_code == "outbox_full" || error_code == "outbox_write_failed";
            return error_reply(request_id, error_code, error_message, retryable);
        }
        outbox_cv_.notify_one();
        return success_reply(request_id, {{"queued", true}}, true);
    }

    return error_reply(request_id, "unsupported_operation", "unsupported operation: " + operation, false);
}

void CloudApiService::outbox_loop() {
    while (running_) {
        bool processed = false;
        try {
            outbox_.maintain(); // Runs even while all pending events are in backoff.
            auto job = outbox_.claim_due();
            if (job) {
                try {
                    process_outbox_job(*job);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("处理云端outbox异常: {}", e.what());
                }
                outbox_.release(*job);
                processed = true;
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("扫描云端outbox异常: {}", e.what());
        }
        std::unique_lock<std::mutex> lock(outbox_mutex_);
        outbox_cv_.wait_for(lock, processed ? std::chrono::milliseconds(50) : std::chrono::seconds(1));
    }
}

void CloudApiService::process_outbox_job(CloudOutbox::Job& event) {
    auto& job = event.metadata;
    try {
        const std::string operation = job.at("operation").get<std::string>();
        const Json& payload = job.at("payload");
        std::string pic_url = job.value("pic_url", "");
        if (pic_url.empty()) {
            const auto image = outbox_.read_image(event);
            const auto upload = platform_client_.upload_image(
                image, payload.value("file_name", "image.jpg"), payload.value("content_type", "image/jpeg"));
            if (!upload.ok) {
                schedule_retry(event, upload);
                return;
            }
            pic_url = upload.result.at("pic_url").get<std::string>();
            job["pic_url"] = pic_url;
            if (!outbox_.checkpoint(event)) {
                CloudPlatformResult failure;
                failure.retryable = true;
                failure.error_code = "outbox_checkpoint_failed";
                failure.error_message = "cannot checkpoint uploaded pic_url";
                schedule_retry(event, failure);
                return;
            }
        }
        const auto report = platform_client_.send_event_report(operation, payload, pic_url);
        if (!report.ok) {
            schedule_retry(event, report);
            return;
        }
        outbox_.complete(event);
        SPDLOG_INFO("云端outbox事件本地WebSocket写入完成: request_id={}, operation={}",
                    job.value("request_id", ""), operation);
    } catch (const std::exception& e) {
        outbox_.fail(event, e.what());
    }
}

void CloudApiService::schedule_retry(CloudOutbox::Job& event, const CloudPlatformResult& failure) {
    if (!failure.retryable) {
        outbox_.fail(event, failure.error_code + ": " + failure.error_message);
        return;
    }
    auto& job = event.metadata;
    // Saturate before incrementing so long-running alarm retries cannot overflow.
    const int attempts = std::clamp(job.value("attempts", 0), 0, 1000000) + 1;
    const int exponent = std::min(attempts - 1, 6);
    const int backoff_seconds = std::min(300, 5 * (1 << exponent));
    static thread_local std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<int> jitter(0, 1000);
    job["attempts"] = attempts;
    job["next_attempt_at_ms"] = unix_time_ms() + backoff_seconds * 1000LL + jitter(generator);
    job["last_error"] = {
        {"code", failure.error_code}, {"message", failure.error_message.substr(0, 512)},
        {"http_status", failure.http_status}
    };
    if (!outbox_.checkpoint(event)) {
        SPDLOG_ERROR("更新云端outbox重试状态失败: {}", event.path.string());
    }
    SPDLOG_INFO("云端outbox发送失败，保留事件并在{}秒后重试: request_id={}, code={}, err={}",
                backoff_seconds, job.value("request_id", ""), failure.error_code, failure.error_message);
}
