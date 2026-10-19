#include "cloud_gateway_client.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cloud_gateway {
namespace {

std::string make_request_id() {
    static thread_local std::mt19937_64 generator(std::random_device{}());
    const auto now = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    const std::array<std::uint64_t, 2> values{generator() ^ now, generator()};

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const auto value : values) stream << std::setw(16) << value;
    return stream.str();
}

RpcReply parse_reply(const nlohmann::json& body, const std::string& request_id) {
    RpcReply reply;
    if (body.value("version", 0) != kProtocolVersion) {
        reply.error_code = "protocol_mismatch";
        reply.error_message = "cloud gateway protocol version mismatch";
        return reply;
    }
    if (body.value("request_id", std::string{}) != request_id) {
        reply.error_code = "request_id_mismatch";
        reply.error_message = "cloud gateway response request_id mismatch";
        return reply;
    }

    reply.ok = body.value("ok", false);
    reply.accepted = body.value("accepted", false);
    reply.upstream_status = body.value("upstream_status", 0);
    if (body.contains("result") && body.at("result").is_object()) {
        reply.result = body.at("result");
    }
    if (body.contains("error") && body.at("error").is_object()) {
        const auto& error = body.at("error");
        reply.error_code = error.value("code", "gateway_error");
        reply.error_message = error.value("message", "cloud gateway request failed");
        reply.retryable = error.value("retryable", false);
    }
    return reply;
}

void set_error(std::string* target, const RpcReply& reply) {
    if (!target) return;
    *target = reply.error_code;
    if (!reply.error_message.empty()) {
        if (!target->empty()) target->append(": ");
        target->append(reply.error_message);
    }
}

} // namespace

CloudGatewayClient::CloudGatewayClient() = default;

RpcReply CloudGatewayClient::request(
    const std::string& operation,
    const nlohmann::json& payload,
    const void* binary_data,
    std::size_t binary_size,
    int timeout_ms
) {
    RpcReply failure;
    try {
        zmq::socket_t socket(context_, zmq::socket_type::req);
        const int linger_ms = 0;
        socket.setsockopt(ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
        socket.setsockopt(ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
        socket.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
        socket.connect(kRpcEndpoint);

        const std::string request_id = make_request_id();
        const nlohmann::json envelope = {
            {"version", kProtocolVersion},
            {"request_id", request_id},
            {"operation", operation},
            {"timeout_ms", timeout_ms},
            {"payload", payload},
            {"has_binary", binary_data != nullptr && binary_size > 0}
        };
        const std::string encoded = envelope.dump();

        const auto metadata_flags = binary_data != nullptr && binary_size > 0
            ? zmq::send_flags::sndmore
            : zmq::send_flags::none;
        if (!socket.send(zmq::buffer(encoded), metadata_flags)) {
            failure.error_code = "gateway_unavailable";
            failure.error_message = "failed to send request to web_gateway";
            failure.retryable = true;
            return failure;
        }
        if (binary_data != nullptr && binary_size > 0) {
            zmq::message_t binary_message(binary_size);
            std::memcpy(binary_message.data(), binary_data, binary_size);
            if (!socket.send(binary_message, zmq::send_flags::none)) {
                failure.error_code = "gateway_unavailable";
                failure.error_message = "failed to send binary payload to web_gateway";
                failure.retryable = true;
                return failure;
            }
        }

        zmq::message_t response;
        if (!socket.recv(response, zmq::recv_flags::none)) {
            failure.error_code = "gateway_timeout";
            failure.error_message = "web_gateway did not reply before the deadline";
            failure.retryable = true;
            return failure;
        }
        const std::string response_text(
            static_cast<const char*>(response.data()), response.size()
        );
        return parse_reply(nlohmann::json::parse(response_text), request_id);
    } catch (const std::exception& e) {
        failure.error_code = "gateway_client_error";
        failure.error_message = e.what();
        failure.retryable = true;
        return failure;
    }
}

std::optional<std::int64_t> CloudGatewayClient::get_server_time(std::string* error) {
    if (error) error->clear();
    const RpcReply reply = request("get_server_time", nlohmann::json::object(), nullptr, 0, 7000);
    if (!reply.ok || !reply.result.contains("timestamp") || !reply.result.at("timestamp").is_number_integer()) {
        if (reply.ok) {
            if (error) *error = "invalid_gateway_response: missing integer timestamp";
        } else {
            set_error(error, reply);
        }
        return std::nullopt;
    }
    return reply.result.at("timestamp").get<std::int64_t>();
}

bool CloudGatewayClient::enqueue_alarm_report(
    const void* image_data,
    std::size_t image_size,
    int alarm_type,
    const std::string& alarm_value,
    const std::string& task_uuid,
    const std::string& event_time,
    std::string* error
) {
    if (error) error->clear();
    const nlohmann::json payload = {
        {"alarm_type", alarm_type},
        {"alarm_value", alarm_value},
        {"task_uuid", task_uuid},
        {"event_time", event_time},
        {"content_type", "image/jpeg"},
        {"file_name", "alarm.jpg"}
    };
    const RpcReply reply = request("enqueue_alarm_report", payload, image_data, image_size, 3000);
    if (!reply.ok || !reply.accepted) {
        set_error(error, reply);
        return false;
    }
    return true;
}

bool CloudGatewayClient::enqueue_capture_report(
    const void* image_data,
    std::size_t image_size,
    const std::string& point_name,
    int action_id,
    const std::string& report_uuid,
    int detection_type,
    const std::string& event_time,
    std::string* error
) {
    if (error) error->clear();
    const nlohmann::json payload = {
        {"point_name", point_name},
        {"action_id", action_id},
        {"report_uuid", report_uuid},
        {"detection_type", detection_type},
        {"event_time", event_time},
        {"content_type", "image/jpeg"},
        {"file_name", "snapshot.jpg"}
    };
    const RpcReply reply = request("enqueue_capture_report", payload, image_data, image_size, 3000);
    if (!reply.ok || !reply.accepted) {
        set_error(error, reply);
        return false;
    }
    return true;
}

} // namespace cloud_gateway
