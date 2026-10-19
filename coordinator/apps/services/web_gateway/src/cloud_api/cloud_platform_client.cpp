#include "cloud_platform_client.hpp"

#include <ctime>
#include <memory>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>
#include "httplib.hpp"

namespace {

bool retryable_status(int status) {
    return status == 408 || status == 425 || status == 429 || status >= 500;
}

std::unique_ptr<httplib::Client> make_http_client(const Config& config) {
    auto client = std::make_unique<httplib::Client>(config.get_platform_http_server_url());
    client->set_connection_timeout(5, 0);
    client->set_read_timeout(10, 0);
    client->set_write_timeout(10, 0);
    client->set_keep_alive(true);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    client->enable_server_certificate_verification(config.get_cloud_tls_verify());
    if (!config.get_cloud_ca_cert_path().empty()) {
        client->set_ca_cert_path(config.get_cloud_ca_cert_path());
    }
#endif
    return client;
}

CloudPlatformResult transport_error(const httplib::Result& response, const std::string& operation) {
    CloudPlatformResult result;
    result.retryable = true;
    result.error_code = "upstream_transport_error";
    result.error_message = operation + ": " + httplib::to_string(response.error());
    return result;
}

CloudPlatformResult http_error(const httplib::Response& response, const std::string& operation) {
    CloudPlatformResult result;
    result.http_status = response.status;
    result.retryable = retryable_status(response.status);
    result.error_code = "upstream_http_error";
    result.error_message = operation + " returned HTTP " + std::to_string(response.status);
    return result;
}

} // namespace

CloudPlatformClient::CloudPlatformClient(WebSocketClient& websocket) : websocket_(websocket) {}

std::string CloudPlatformClient::api_path(const std::string& relative_path) const {
    return config_->get_platform_http_api_path(relative_path);
}

CloudPlatformResult CloudPlatformClient::get_server_time() const {
    try {
        auto client = make_http_client(*config_);
        const auto response = client->Get(api_path("/device/device/asyncLocalTime"));
        if (!response) return transport_error(response, "get_server_time");
        if (response->status != 200) return http_error(*response, "get_server_time");

        const auto body = nlohmann::json::parse(response->body);
        if (!body.contains("timestamp") || !body.at("timestamp").is_number_integer()) {
            CloudPlatformResult invalid;
            invalid.error_code = "invalid_upstream_response";
            invalid.error_message = "get_server_time response has no integer timestamp";
            return invalid;
        }

        CloudPlatformResult result;
        result.ok = true;
        result.http_status = response->status;
        result.result = {{"timestamp", body.at("timestamp")}};
        return result;
    } catch (const std::exception& e) {
        CloudPlatformResult result;
        result.retryable = true;
        result.error_code = "upstream_client_error";
        result.error_message = e.what();
        return result;
    }
}

CloudPlatformResult CloudPlatformClient::upload_image(
    const std::string& image,
    const std::string& file_name,
    const std::string& content_type
) const {
    try {
        auto client = make_http_client(*config_);
        const httplib::MultipartFormDataItems items = {
            {"file", image, file_name, content_type},
            {"type", "14", "", ""},
            {"sn", config_->get_sn(), "", ""}
        };
        const auto response = client->Post(api_path("/device/device/uploadImage"), items);
        if (!response) return transport_error(response, "upload_image");
        if (response->status != 200) return http_error(*response, "upload_image");

        const auto body = nlohmann::json::parse(response->body);
        const std::string pic_url = body.at("result").value("picUrl", "");
        if (pic_url.empty() || pic_url.size() > 2048) {
            CloudPlatformResult invalid;
            invalid.error_code = "invalid_upstream_response";
            invalid.error_message = "upload_image response has no valid result.picUrl (1..2048 bytes)";
            return invalid;
        }

        CloudPlatformResult result;
        result.ok = true;
        result.http_status = response->status;
        result.result = {{"pic_url", pic_url}};
        return result;
    } catch (const nlohmann::json::exception& e) {
        CloudPlatformResult result;
        result.error_code = "invalid_upstream_response";
        result.error_message = e.what();
        return result;
    } catch (const std::exception& e) {
        CloudPlatformResult result;
        result.retryable = true;
        result.error_code = "upstream_client_error";
        result.error_message = e.what();
        return result;
    }
}

CloudPlatformResult CloudPlatformClient::send_event_report(
    const std::string& operation,
    const nlohmann::json& payload,
    const std::string& pic_url
) const {
    try {
        nlohmann::json message;
        if (operation == "enqueue_alarm_report") {
            message = {
                {"Header", {
                    {"CmdType", "Robot_Alarm_info_Push"},
                    {"Sn", config_->get_sn()},
                    {"Time", std::time(nullptr)}
                }},
                {"PatrolAlarmInfo", {
                    {"picUrl", pic_url},
                    {"time", payload.at("event_time")},
                    {"alarmType", payload.at("alarm_type")},
                    {"alarmValue", payload.at("alarm_value")},
                    {"uuid", payload.at("task_uuid")}
                }}
            };
        } else if (operation == "enqueue_capture_report") {
            message = {
                {"Header", {
                    {"CmdType", "Robot_Capture_info_Push"},
                    {"Sn", config_->get_sn()},
                    {"Time", std::time(nullptr)}
                }},
                {"PatrolCaptureInfo", {
                    {"picUrl", pic_url},
                    {"time", payload.at("event_time")},
                    {"beaconPointCode", payload.at("point_name")},
                    {"lineCode", ""},
                    {"actionUnitId", payload.at("action_id")},
                    {"uuid", payload.at("report_uuid")},
                    {"detectionType", payload.at("detection_type")}
                }}
            };
        } else {
            CloudPlatformResult invalid;
            invalid.error_code = "unsupported_operation";
            invalid.error_message = "unsupported outbox operation: " + operation;
            return invalid;
        }

        if (!websocket_.send_message_and_wait(message.dump())) {
            CloudPlatformResult result;
            result.retryable = true;
            result.error_code = "websocket_write_failed";
            result.error_message = "cloud websocket local write failed or timed out";
            return result;
        }

        CloudPlatformResult result;
        result.ok = true;
        return result;
    } catch (const std::exception& e) {
        CloudPlatformResult result;
        result.error_code = "invalid_event_payload";
        result.error_message = e.what();
        return result;
    }
}
