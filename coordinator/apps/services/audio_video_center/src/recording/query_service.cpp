#include <filesystem>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "query_service.hpp"

RecordingQueryService::RecordingQueryService() {
    socket.bind("ipc://@recording_center_files_query");
    socket_web.connect("tcp://127.0.0.1:2007");
}

RecordingQueryService::~RecordingQueryService() {
    stop();
}

void RecordingQueryService::start() {
    if (running) {
        SPDLOG_WARN("录像文件查询服务已经在运行了");
        return;
    }
    running = true;
    worker_thread = std::thread(&RecordingQueryService::message_loop, this);
    SPDLOG_INFO("录像文件查询服务已启动");
}

void RecordingQueryService::stop() {
    if (!running) return;
    running = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    SPDLOG_INFO("StreamingService stopped");
}

void RecordingQueryService::message_loop() {
    while (true) {
        try {
            zmq::message_t zmq_message;
            auto recv_result = socket.recv(zmq_message, zmq::recv_flags::none);
            if (!recv_result) continue;
            std::string message_string(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("录像文件查询线程收到消息: {}", message_string);

            nlohmann::json message = nlohmann::json::parse(message_string);
            std::string channel = message.at("channel").get<std::string>();
            std::string type = message.at("type").get<std::string>();
            std::string start_time = message.value("start_time", "");
            std::string end_time = message.value("end_time", "");

            if (type != "recording-query") continue;
            handle_view_record(channel, start_time, end_time);

        } catch (const std::exception& e) {
            SPDLOG_ERROR("录像文件查询线程异常: {}", e.what());
        }
    }
}

void RecordingQueryService::handle_view_record(const std::string& channel, const std::string& begin_time, const std::string& end_time) {
    using namespace nlohmann;
    namespace fs = std::filesystem;
    constexpr int VIDEO_DURATION_SECONDS = 20 * 60;
    
    auto list = json::array();
    std::string recordings_dir = "/datas/recordings/" + channel;
    if (fs::exists(recordings_dir) && fs::is_directory(recordings_dir)) {
        // 收集并排序
        std::vector<std::string> filenames;
        for (const auto& entry : fs::directory_iterator(recordings_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
                filenames.push_back(entry.path().filename().string());
            }
        }
        std::sort(filenames.begin(), filenames.end());
        
        for (const auto& filename : filenames) {
            std::string start_time = parse_filename_to_datetime(filename, channel);
            if (start_time.empty()) continue;
            if (start_time < begin_time || start_time > end_time) continue;
            
            // 结束时间 = 开始时间 + 20分钟
            std::string file_end_time = add_seconds_to_datetime(start_time, VIDEO_DURATION_SECONDS);
            list.push_back({{"Filename", filename}, {"StartTime", start_time}, {"EndTime", file_end_time}});
        }
    }
    
    json params = {
        {"Header", {
            {"CmdType", "RC_Robot_Offline_Videolist_Get"},
            {"Sn", config->get_sn()},
            {"Time", std::to_string(std::time(nullptr))}
        }},
        {"OfflineVideoList", {{"Info", list}}}
    };
    socket_web.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("发送平台回复: {}", params.dump());
}

std::string RecordingQueryService::parse_filename_to_datetime(const std::string& filename, const std::string& channel) {
    // 期望格式: {channel}_{YYYYMMDD}_{HHMMSS}.mp4
    std::string prefix = channel + "_";
    
    if (filename.find(prefix) != 0) {
        return "";
    }
    
    // 提取时间部分: 20260114_223433
    size_t start = prefix.length();
    size_t end = filename.rfind(".mp4");
    
    if (end == std::string::npos || end <= start) {
        return "";
    }
    
    std::string time_part = filename.substr(start, end - start);
    
    // 解析: 20260114_223433
    if (time_part.length() != 15 || time_part[8] != '_') {
        return "";
    }
    
    // 转换为: 2026-01-14 22:34:33
    std::string datetime = time_part.substr(0, 4) + "-" + // 年
        time_part.substr(4, 2) + "-" + // 月
        time_part.substr(6, 2) + " " + // 日
		time_part.substr(9, 2) + ":" + // 时
		time_part.substr(11, 2) + ":" + // 分
		time_part.substr(13, 2); // 秒
    return datetime;
}

std::string RecordingQueryService::add_seconds_to_datetime(const std::string& datetime, int seconds) {
    std::tm tm = {};
    std::istringstream ss(datetime);
    ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
    if (ss.fail()) {
        return datetime;
    }
    
    std::time_t time = std::mktime(&tm);
    time += seconds;
    std::tm* new_tm = std::localtime(&time);
    std::ostringstream oss;
    oss << std::put_time(new_tm, "%Y-%m-%d %H:%M:%S");
    
    return oss.str();
}
