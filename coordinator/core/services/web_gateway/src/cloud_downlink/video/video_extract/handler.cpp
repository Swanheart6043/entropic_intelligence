#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "handler.hpp"
#include "httplib.hpp"

namespace {
    constexpr auto max_extract_duration = std::chrono::seconds(120);
    constexpr std::uintmax_t max_video_size = 300ULL * 1024ULL * 1024ULL;

    bool parse_local_time(const std::string& value, std::time_t& result) {
        std::tm time_value{};
        time_value.tm_isdst = -1;

        std::istringstream stream(value);
        stream >> std::get_time(&time_value, "%Y-%m-%d %H:%M:%S");
        if (stream.fail()) return false;

        stream >> std::ws;
        if (!stream.eof()) return false;

        result = std::mktime(&time_value);
        return result != static_cast<std::time_t>(-1);
    }
}

ExtractorHandler::ExtractorHandler(WebSocketClient& websocket_connect) : websocket(websocket_connect) {
    upload_path = config->get_platform_http_api_path("/file/uploadVideoRecord");
}

ExtractorHandler::~ExtractorHandler() {
    stop();
}

void ExtractorHandler::start() {
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        SPDLOG_WARN("录像文件截取线程已经启动");
        return;
    }
    try {
        worker_thread = std::thread(&ExtractorHandler::worker_loop, this);
        SPDLOG_INFO("录像文件截取线程已启动");
    } catch (...) {
        running = false;
        throw;
    }
}

void ExtractorHandler::stop() {
    if (!running.exchange(false)) return;

    queue_condition.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        pending_tasks.clear();
    }
    SPDLOG_INFO("录像文件截取线程已停止");
}

bool ExtractorHandler::submit(const std::string& message) {
    send_ack();

    ExtractionTask task;
    std::string error_message;

    if (!parse_task(message, task, error_message)) {
        SPDLOG_WARN("拒绝录像截取任务: {}", error_message);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        if (!running) {
            error_message = "录像截取服务未启动";
        } else if (pending_tasks.size() >= max_pending_tasks) {
            error_message = "录像截取任务繁忙";
        } else {
            pending_tasks.push_back(std::move(task));
        }
    }

    if (!error_message.empty()) {
        SPDLOG_WARN("拒绝录像截取任务: {}", error_message);
        return false;
    }

    queue_condition.notify_one();
    SPDLOG_INFO("录像截取任务已加入队列");
    return true;
}

void ExtractorHandler::worker_loop() {
    while (true) {
        ExtractionTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_condition.wait(lock, [this] {
                return !running || !pending_tasks.empty();
            });

            if (!running) break;

            task = std::move(pending_tasks.front());
            pending_tasks.pop_front();
        }

        try {
            process_task(task);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("录像截取任务异常: {}", e.what());
        } catch (...) {
            SPDLOG_ERROR("录像截取任务发生未知异常");
        }
    }
}

void ExtractorHandler::process_task(const ExtractionTask& task) {
    std::string extracted_file = video_time_range_extractor.extract_video_range(
        task.channel,
        task.start_time,
        task.end_time
    );
    if (extracted_file.empty()) {
        SPDLOG_ERROR("视频截取失败, uuid: {}", task.uuid);
        return;
    }
    SPDLOG_INFO("视频截取成功，文件路径: {}, uuid: {}", extracted_file, task.uuid);

    bool upload_success = upload_file_to_server(extracted_file, task.uuid);
    if (upload_success) {
        SPDLOG_INFO("视频上传成功: {}, uuid: {}", extracted_file, task.uuid);
    } else {
        SPDLOG_ERROR("视频上传失败: {}, uuid: {}", extracted_file, task.uuid);
    }

    std::error_code remove_error;
    if (!std::filesystem::remove(extracted_file, remove_error) && remove_error) {
        SPDLOG_WARN("删除截取文件失败: {}, 原因: {}", extracted_file, remove_error.message());
    } else {
        SPDLOG_INFO("已删除截取文件: {}", extracted_file);
    }
}

bool ExtractorHandler::parse_task(
    const std::string& message,
    ExtractionTask& task,
    std::string& error_message
) const {
    try {
        const nlohmann::json message_json = nlohmann::json::parse(message);
        const std::string channel_num = message_json.value("ChannelNum", "");
        task.start_time = message_json.value("QueryBeginTime", "");
        task.end_time = message_json.value("QueryEndTime", "");
        task.uuid = message_json.value("UUID", "");

        if (channel_num == "1") task.channel = "front";
        else if (channel_num == "2") task.channel = "high_definition";
        else if (channel_num == "3") task.channel = "infrared";
        else if (channel_num == "4") task.channel = "rear";
        else if (channel_num == "5") task.channel = "left";
        else if (channel_num == "6") task.channel = "right";
        else {
            error_message = "录像通道无效";
            return false;
        }

        if (task.uuid.empty()) {
            error_message = "UUID不能为空";
            return false;
        }

        std::time_t start_timestamp{};
        std::time_t end_timestamp{};
        if (!parse_local_time(task.start_time, start_timestamp) ||
            !parse_local_time(task.end_time, end_timestamp)) {
            error_message = "录像时间格式无效";
            return false;
        }

        const auto duration = std::chrono::seconds(
            static_cast<long long>(std::difftime(end_timestamp, start_timestamp))
        );
        if (duration <= std::chrono::seconds::zero()) {
            error_message = "录像结束时间必须晚于开始时间";
            return false;
        }
        if (duration > max_extract_duration) {
            error_message = "录像截取时长不能超过2分钟";
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        error_message = std::string("录像截取参数无效: ") + e.what();
        return false;
    }
}

void ExtractorHandler::send_ack() {
    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "RC_Robot_Offline_RecordVideo_Get"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1},
        {"Data", ""}
    };
    websocket.send_message(ack.dump());
    SPDLOG_INFO("发送平台回复: {}", ack.dump());
}

bool ExtractorHandler::upload_file_to_server(const std::string& file_path, const std::string& uuid) {
    try {
        if (!std::filesystem::exists(file_path)) {
            SPDLOG_ERROR("文件不存在: {}", file_path);
            return false;
        }

        const auto file_size = std::filesystem::file_size(file_path);
        SPDLOG_INFO("准备上传文件: {}, 大小: {} bytes", file_path, file_size);
        if (file_size > max_video_size) {
            SPDLOG_ERROR("视频文件超过300 MiB限制: {}, 大小: {} bytes", file_path, file_size);
            return false;
        }

        std::ifstream file(file_path, std::ios::binary);
        if (!file) {
            SPDLOG_ERROR("无法打开文件: {}", file_path);
            return false;
        }

        std::string file_content(
            (std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>()
        );
        file.close();

        httplib::Client client(config->get_platform_http_server_url());
        client.set_connection_timeout(10, 0);
        client.set_read_timeout(300, 0);
        client.set_write_timeout(300, 0);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        client.enable_server_certificate_verification(config->get_cloud_tls_verify());
        if (!config->get_cloud_ca_cert_path().empty()) {
            client.set_ca_cert_path(config->get_cloud_ca_cert_path());
        }
#endif

        const std::filesystem::path path(file_path);
        const std::string filename = path.filename().string();

        httplib::MultipartFormDataItems items = {
            {"file", file_content, filename, "video/mp4"},
            {"code", config->get_sn()},
            {"uuid", uuid},
        };
        SPDLOG_INFO("开始上传到: {}, code: {}, uuid: {}", upload_path, config->get_sn(), uuid);

        auto res = client.Post(upload_path, items);
        if (!res) {
            SPDLOG_ERROR("HTTP 请求失败: {}", httplib::to_string(res.error()));
            return false;
        }
        if (res->status == 200 || res->status == 201) {
            SPDLOG_INFO("上传成功, HTTP 状态码: {}, 响应: {}", res->status, res->body);
            return true;
        }

        SPDLOG_ERROR("上传失败, HTTP 状态码: {}, 响应: {}", res->status, res->body);
        return false;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("上传异常: {}", e.what());
        return false;
    }
}
