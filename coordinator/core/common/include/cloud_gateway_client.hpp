#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

namespace cloud_gateway {

inline constexpr const char* kRpcEndpoint = "ipc://@cloud_api_rpc";
inline constexpr int kProtocolVersion = 1;

struct RpcReply {
    bool ok{false};
    bool accepted{false};
    bool retryable{false};
    int upstream_status{0};
    std::string error_code;
    std::string error_message;
    nlohmann::json result = nlohmann::json::object();
};

// 业务进程只依赖稳定的语义接口，不感知云端 URL、路径、鉴权或协议方案。
// 每次请求创建独立 REQ socket，避免超时后的 EFSM 状态污染下一次调用。
class CloudGatewayClient {
public:
    CloudGatewayClient();

    std::optional<std::int64_t> get_server_time(std::string* error = nullptr);

    bool enqueue_alarm_report(
        const void* image_data,
        std::size_t image_size,
        int alarm_type,
        const std::string& alarm_value,
        const std::string& task_uuid,
        const std::string& event_time,
        std::string* error = nullptr
    );

    bool enqueue_capture_report(
        const void* image_data,
        std::size_t image_size,
        const std::string& point_name,
        int action_id,
        const std::string& report_uuid,
        int detection_type,
        const std::string& event_time,
        std::string* error = nullptr
    );

private:
    zmq::context_t context_{1};

    RpcReply request(
        const std::string& operation,
        const nlohmann::json& payload,
        const void* binary_data,
        std::size_t binary_size,
        int timeout_ms
    );
};

} // namespace cloud_gateway
