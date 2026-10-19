#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "sole.hpp"
#include "db_connection.hpp"
#include "state_subscriber.hpp"
#include "perceiver_monitor.hpp"
#include "info_synchronizer.hpp"

PerceiverCenterServer::PerceiverCenterServer() {
    const std::string log_dir = "/home/heyn/patrol_robot_h1/logs/monitor_center";
    const std::string log_path = log_dir + "/perceiver.log";

    create_log(log_path);
    clear_old_logs(log_dir, 5);
    
    // push pull相关
    int timeout_ms = 3000;
    int linger = 0;
    
    socket_from_perceiver.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_from_perceiver.bind("tcp://*:2425");
    
    socket_push_task_center.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_push_task_center.connect("tcp://127.0.0.1:8223");
    
    socket_req_task_center.setsockopt(ZMQ_SNDTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_req_task_center.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_req_task_center.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_req_task_center.connect("ipc://@task_center_req_rep");
}

PerceiverCenterServer::~PerceiverCenterServer() {
    stop();
    if (logger) logger->flush();
}

void PerceiverCenterServer::start() {
    if (running.exchange(true)) {
        logger->warn("感知交互线程，已经在启动了");
        return;
    }
    receiver_thread = std::thread(&PerceiverCenterServer::receiver_loop, this);
    worker_thread = std::thread(&PerceiverCenterServer::worker_loop, this);
    sender_thread = std::thread(&PerceiverCenterServer::sender_loop, this);
}

void PerceiverCenterServer::stop() {
    if (!running.exchange(false)) {
        logger->warn("感知交互线程未在运行, 无需停止");
        return;
    }
    // 唤醒可能阻塞在 pop 上的 worker
    message_queue.notify_all();
    if (receiver_thread.joinable()) receiver_thread.join();
    if (worker_thread.joinable()) worker_thread.join();
    if (sender_thread.joinable()) sender_thread.join();
}

void PerceiverCenterServer::receiver_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto has_message = socket_from_perceiver.recv(zmq_message, zmq::recv_flags::none);
            if (!has_message) continue; // RCVTIMEO 超时，回去检查 running
            message_queue.push(std::string(static_cast<char*>(zmq_message.data()), zmq_message.size()));
        } catch (const std::exception& e) {
            logger->error("IPC 命令监听线程异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void PerceiverCenterServer::worker_loop() {
    while (running) {
        auto msg = message_queue.pop(running);
        if (!msg) break;
        try {
            handle_message(*msg);
        } catch (const std::exception& e) {
            logger->error("处理检测结果异常: {}", e.what());
        }
    }
}

void PerceiverCenterServer::handle_message(const std::string& message) {
    DetectionResult result = parse_detection(message);
    if (result.detection_type.empty()) {
        logger->warn("检测结果缺少 detection_type，跳过");
        return;
    }
    logger->info("检测到类型: {}, 最大置信度: {}", result.detection_type, result.max_confidence);

    static const std::unordered_map<std::string, int> alarm_map = {
        {"smoking", 488},
        {"no_safety_helmet", 426},
        {"no_reflective_vest", 1023},
        {"rubbish", 10016},
        {"thermal_overtemp", 223},
        {"pet", 441},
        {"fire", 311},
        {"ground_water", 10008},
        {"interchangeable", 317},
        {"wear_safety_helmet", 10055},
        {"wear_reflective_vest", 10055},
        {"door", 1005},
        {"breaking_into_penalty_area", 102},
        {"cats_and_dogs", 441},
        {"illegal_parking_of_non_motorized_vehicles", 1022},
        {"the_trash_can_was_overflowing", 1008},
        {"randomly_piled_up_clutter", 10054},
        {"carrying_knives", 493},
        {"illegally_parked_vehicles", 310},
    };
    auto it = alarm_map.find(result.detection_type);
    if (it == alarm_map.end()) {
        logger->error("未知检测类型: {}", result.detection_type);
        return;
    }
    int alarm_type = it->second;

    // 本地告警动作不依赖云端，网络异常不能阻止机器人本地响应。
    send_alarm_actions(result.detection_type, alarm_type);

    if (result.image_data.empty()) {
        logger->error("检测结果无图像数据，跳过上传");
        return;
    }

    // uuid 请求。失败返回空串
    auto uuid = request_task_uuid();
    if (uuid.empty()) logger->error("获取uuid为空，可能没任务");

    std::string gateway_error;
    const bool queued = cloud_gateway_.enqueue_alarm_report(
        result.image_data.data(),
        result.image_data.size(),
        alarm_type,
        result.all_confidences,
        uuid,
        format_now_date_to_string(),
        &gateway_error
    );
    if (!queued) {
        logger->error("告警上报写入web_gateway队列失败: {}", gateway_error);
        return;
    }
    logger->info("告警上报已写入web_gateway持久化队列: alarm_type={}", alarm_type);
}

DetectionResult PerceiverCenterServer::parse_detection(const std::string& json_str) {
    DetectionResult result{};
    try {
        using json = nlohmann::json;
        json j = json::parse(json_str);
        result.type = j.value("type", "");
        result.detection_type = j.value("detection_type", "");
        result.frame_id = j.value("frame_id", 0);
        result.timestamp = j.value("timestamp", 0.0);
        result.image_width = j.value("image_width", 0);
        result.image_height = j.value("image_height", 0);
        result.avg_confidence = j.value("avg_confidence", 0.0f);
        result.max_confidence = j.value("max_confidence", 0.0f);
        result.all_confidences = j.value("all_confidences", "");

        if (j.contains("detections")) {
            for (const auto& det : j["detections"]) {
                Detection d;
                d.class_id = det.value("class_id", 0);
                d.class_name = det.value("class_name", "unknown");
                d.confidence = det.value("confidence", 0.0f);
                if (det.contains("bbox")) {
                    const auto& bbox = det["bbox"];
                    d.x1 = bbox.value("x1", 0);
                    d.y1 = bbox.value("y1", 0);
                    d.x2 = bbox.value("x2", 0);
                    d.y2 = bbox.value("y2", 0);
                    d.width = bbox.value("width", 0);
                    d.height = bbox.value("height", 0);
                }
                result.detections.push_back(d);
            }
        }
        if (j.contains("image_base64")) {
            std::string img_base64 = j["image_base64"];
            result.image_data = decode_base64(img_base64);
        }
    } catch (const std::exception& e) {
        logger->error("JSON 解析错误: {}", e.what());
    }
    return result;
}

std::vector<AlarmAction> PerceiverCenterServer::query_alarm_actions(int code) {
    std::vector<AlarmAction> actions;
    sql::Connection* con = DBConnection::get_instance().get_connection();

    std::unique_ptr<sql::PreparedStatement> stmt(con->prepareStatement(R"(
        SELECT alarm_type, action_type, action_value1, action_value2, action_wait_ms
        FROM alarm_action_config
        WHERE alarm_type = ?
        ORDER BY unit_order ASC
    )"));
    stmt->setString(1, std::to_string(code));
    std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());

    while (rs->next()) {
        actions.push_back({
            rs->getString("alarm_type"),
            rs->getString("action_type"),
            rs->getString("action_value1"),
            rs->getString("action_value2"),
            rs->getInt("action_wait_ms")
        });
    }
    return actions;
}

void PerceiverCenterServer::send_alarm_actions(const std::string& type, int code) {
    if (type.empty()) {
        logger->warn("告警 type 不能为空");
        return;
    }
    if (code <= 0) {
        logger->warn("告警 code 非法: {}", code);
        return;
    }

    std::vector<AlarmAction> actions;
    try {
        actions = query_alarm_actions(code);
    } catch (const sql::SQLException& e) {
        logger->warn("查询告警 {} 的处置动作失败: {}", code, e.what());
        return;
    }
    if (actions.empty()) {
        logger->warn("告警 {} 未配置处置动作，跳过", code);
        return;
    }

    nlohmann::json message = {{"command", "alarm-actions"}, {"action_list", actions}};
    socket_push_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
}

std::string PerceiverCenterServer::request_task_uuid() {
    nlohmann::json params = {{"command", "patrol_duty_uuid"}};

    // DEALER 对接 REP：需要 [空帧][数据帧]
    auto empty_sent = socket_req_task_center.send(zmq::message_t{}, zmq::send_flags::sndmore);
    if (!empty_sent) {
        logger->error("发送uuid请求失败(空帧)");
        return "";
    }
    auto send_result = socket_req_task_center.send(zmq::buffer(params.dump()), zmq::send_flags::none);
    if (!send_result) {
        logger->error("发送uuid请求失败");
        return "";
    }

    // 读回：[空帧][数据帧]
    zmq::message_t delim;
    auto d = socket_req_task_center.recv(delim, zmq::recv_flags::none);
    if (!d) {
        logger->error("接收uuid响应超时(空帧)");
        return "";
    }
    zmq::message_t reply;
    auto recv_result = socket_req_task_center.recv(reply, zmq::recv_flags::none);
    if (!recv_result) {
        logger->error("接收uuid响应超时");
        return "";
    }

    try {
        auto rep_result = nlohmann::json::parse(std::string_view(static_cast<const char*>(reply.data()), reply.size()));
        std::string uuid = rep_result.at("uuid").get<std::string>();
        return uuid;
    } catch (const std::exception& e) {
        logger->error("解析uuid响应失败: {}", e.what());
        return "";
    }
}

void PerceiverCenterServer::clear_old_logs(const std::string& log_dir, int max_day) {
    namespace fs = std::filesystem;
    if (!fs::exists(log_dir)) return;
    auto now = fs::file_time_type::clock::now();
    std::regex log_pattern(R"(perceiver_\d{4}-\d{2}-\d{2}\.log)");

    for (const auto& entry : fs::directory_iterator(log_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (!std::regex_match(filename, log_pattern)) continue;

        auto file_time = fs::last_write_time(entry);
        auto file_age = now - file_time;
        auto hours_old = std::chrono::duration_cast<std::chrono::hours>(file_age).count();
        int days_old = static_cast<int>(hours_old / 24);

        if (days_old > max_day) {
            logger->info("删除过期日志: {} ({}天前)", filename, days_old);
            std::error_code ec;
            fs::remove(entry.path(), ec);
            if (ec) logger->error("删除失败: {} - {}", filename, ec.message());
        }
    }
}

void PerceiverCenterServer::create_log(const std::string& log_path) {
    auto sink = std::make_shared<spdlog::sinks::daily_file_sink_mt>(log_path, 0, 0, false, 5);
    logger = std::make_shared<spdlog::logger>("perceiver", sink);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %v");
    logger->flush_on(spdlog::level::info);
}

void PerceiverCenterServer::sender_loop() {
    BasicInfoSender basic_info_sender;
    basic_info_sender.start();
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    basic_info_sender.stop();
}

std::vector<uint8_t> PerceiverCenterServer::decode_base64(const std::string& encoded) {
    static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<uint8_t> decoded;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (c == '\n' || c == '\r' || c == ' ') continue; // 跳过换行/空白，不截断
        if (c == '=') break; // 填充符，正常结束
        if (T[c] == -1) break; // 非法字符，停止
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            decoded.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return decoded;
}

std::string PerceiverCenterServer::format_now_date_to_string() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
    return ss.str();
}
