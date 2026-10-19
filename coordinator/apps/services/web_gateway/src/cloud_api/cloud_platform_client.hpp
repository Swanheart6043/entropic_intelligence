#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

#include "config_parser.hpp"
#include "common/wss_connector.hpp"

struct CloudPlatformResult {
    bool ok{false};
    bool retryable{false};
    int http_status{0};
    std::string error_code;
    std::string error_message;
    nlohmann::json result = nlohmann::json::object();
};

class CloudPlatformClient {
public:
    explicit CloudPlatformClient(WebSocketClient& websocket);

    CloudPlatformResult get_server_time() const;
    CloudPlatformResult upload_image(
        const std::string& image,
        const std::string& file_name,
        const std::string& content_type
    ) const;
    CloudPlatformResult send_event_report(
        const std::string& operation,
        const nlohmann::json& payload,
        const std::string& pic_url
    ) const;

private:
    Config* config_{Config::getInstance()};
    WebSocketClient& websocket_;

    std::string api_path(const std::string& relative_path) const;
};
