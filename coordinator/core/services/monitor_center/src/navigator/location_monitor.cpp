#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"
#include "db_connection.hpp"
#include "state_manager/state_manager.hpp"
#include "broadcast_template.hpp"
#include "location_monitor.hpp"

LocationMonitor::LocationMonitor() : last_broadcast_time(std::chrono::steady_clock::now() - std::chrono::seconds(10)) {
    socket_web.connect("tcp://127.0.0.1:2007");
    socket_android.connect("ipc://@edge_gateway_push_pull");
    rebuild_perceiver_socket();
}

LocationMonitor::~LocationMonitor() {
    stop();
}

void LocationMonitor::start() {
    if (running) {
        SPDLOG_WARN("位置监控线程，已经在启动了");
        return;
    }
    running = true;
    query_detection_area();
    worker_thread = std::thread(&LocationMonitor::monitoring, this);
}

void LocationMonitor::stop() {
    if (!running) {
        SPDLOG_WARN("位置监控线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void LocationMonitor::monitoring() {
    using namespace nlohmann;

    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        auto initialize_result = http.Get("/gs-robot/real_time_data/current_initialize_status");
        if (!initialize_result || initialize_result->status != 200) {
            socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
            SPDLOG_ERROR("导航挂死: {}", message.dump());
            continue;
        }
        
        if (initialize_result->body.empty()) {
            socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
            SPDLOG_ERROR("获取初始化失败: {}", message.dump());
            continue;
        }
        
        current_map = json::parse(initialize_result->body).value("currentMap", "");
        if (current_map.empty()) {
            handle_location_loss();
            continue;
        }
        
        handle_location_not_lost();
    }
}

void LocationMonitor::handle_location_loss() {
    using namespace nlohmann;

    socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    SPDLOG_WARN("推送定位丢失告警: {}", message.dump());

    // 自动模式下广播定位丢失
    auto global_state_json = GlobalStateManager::getInstance().get_global_state().to_json();
    if (global_state_json.at("mode").get<std::string>() == "manual") {
        SPDLOG_DEBUG("手动模式不播放声音");
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_broadcast_time);
    if (elapsed.count() >= 10) {
        BroadcastTemplate broadcast_template{"location.lost"};
        json params = {
            {"command", "broadcast-single-text"}, 
            {"content", broadcast_template.get_content("定位丢失, 请手动初始化")}
        };
        socket_android.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
        // 更新广播时间
        last_broadcast_time = now;
    }
}

void LocationMonitor::handle_location_not_lost() {
    using namespace nlohmann;

    json message1 = {
        {"Header", {
            {"CmdType", "PhoenixStatusPost"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"CurrentTask", {
            {"TaskInfo", {
                {"MapName", current_map}
            }}
        }}
    };
    socket_web.send(zmq::buffer(message1.dump()), zmq::send_flags::dontwait);

    // 实时位置
    auto real_position_result = http.Get("/gs-robot/real_time_data/position");
    if (!real_position_result || real_position_result->status != 200) {
        SPDLOG_WARN("获取实时位置失败");
        return;
    }

    auto global_state_json = GlobalStateManager::getInstance().get_global_state().to_json();
    auto gps_info = global_state_json.at("gps");
    auto valid = gps_info.at("valid").get<int>();
    auto sn = gps_info.at("sn").get<int>();
    auto ew = gps_info.at("ew").get<int>();
    auto longitude = gps_info.at("longitude").get<int>();
    auto latitude = gps_info.at("latitude").get<int>();
    auto altitude = gps_info.at("asl").get<int>();
    auto speed = gps_info.at("speed").get<int>();
    auto direction = gps_info.at("direction").get<int>();

    auto position = json::parse(real_position_result->body);
    double angle = position.value("angle", 0.000);
    double x = position.at("gridPosition").value("x", 0.000);
    double y = position.at("gridPosition").value("y", 0.000);
    json real_time_position = {
        {"Header", {
            {"CmdType", "NavStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"RobotPositionInfo", {
            {"gridPosition", {
                {"mapName", current_map},
                {"x", x},
                {"y", y},
                {"angle", angle}
            }},
            {"GPS", {
                {"ew", ew},
                {"sn", sn},
                {"longitude", longitude},
                {"latitude", latitude},
                {"altitude", altitude},
                {"speed", speed},
                {"direction", direction},
                {"status", valid}
            }},
        }}
    };
    socket_web.send(zmq::buffer(real_time_position.dump()), zmq::send_flags::dontwait);
    SPDLOG_DEBUG("获取初始化信息成功: {}", real_time_position.dump());

    check_in_detection_area(current_map, x, y);
}

bool LocationMonitor::query_detection_area() {
    std::unordered_map<std::string, std::vector<DetectionArea>> new_cache;
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        auto sql = "SELECT map_name, area_data FROM algorithm_areas";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        while (res->next()) {
            const std::string map_name = res->getString("map_name");
            const std::string json_text = res->getString("area_data");

            auto area_array = nlohmann::json::parse(json_text);
            if (!area_array.is_array()) {
                SPDLOG_WARN("地图 {} 的 area_data 不是数组", map_name);
                continue;
            }
            for (const auto& item : area_array) {
                if (!item.contains("points") ||
                    !item["points"].is_array() ||
                    item["points"].size() < 3) {
                    SPDLOG_WARN("地图 {} 存在无效算法区域", map_name);
                    continue;
                }

                DetectionArea area;
                area.algorithm_name = item.value("algorithmName", "");
                area.algorithm_type = item.value("algorithmType", -1);
                for (const auto& point : item["points"]) {
                    if (!point.contains("x") ||
                        !point.contains("y") ||
                        !point["x"].is_number() ||
                        !point["y"].is_number()) {
                        continue;
                    }
                    area.points.push_back({
                        point["x"].get<double>(),
                        point["y"].get<double>()
                    });
                }
                if (!area.algorithm_name.empty() &&
                    area.algorithm_type >= 0 &&
                    area.points.size() >= 3) {
                    new_cache[map_name].push_back(std::move(area));
                }
            }
        }
        // 全部加载成功后再替换，避免半成品缓存
        detection_areas_.swap(new_cache);
        SPDLOG_INFO("算法区域缓存加载完成，共 {} 张地图", detection_areas_.size());
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("加载算法区域失败: {}", e.what());
        return false;
    }
}

void LocationMonitor::check_in_detection_area(const std::string& map_name, double x, double y) {
    std::set<AlgorithmKey> current_algorithms;
    auto map_it = detection_areas_.find(map_name);

    if (map_it != detection_areas_.end()) {
        for (const auto& area : map_it->second) {
            if (point_in_polygon(x, y, area.points)) {
                current_algorithms.emplace(area.algorithm_type, area.algorithm_name);
            }
        }
    }
    // 新进入的算法区域
    for (const auto& algorithm : current_algorithms) {
        if (active_algorithms_.count(algorithm) == 0) {
            send_detection_command("on", algorithm.second, algorithm.first);
        }
    }
    // 已离开的算法区域
    for (const auto& algorithm : active_algorithms_) {
        if (current_algorithms.count(algorithm) == 0) {
            send_detection_command("off", algorithm.second, algorithm.first);
        }
    }
    active_algorithms_ = std::move(current_algorithms);
}

bool LocationMonitor::point_on_segment(double x, double y, const Point& a, const Point& b) {
    constexpr double eps = 1e-8;
    double cross = (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    if (std::abs(cross) > eps) {
        return false;
    }
    return x >= std::min(a.x, b.x) - eps &&
           x <= std::max(a.x, b.x) + eps &&
           y >= std::min(a.y, b.y) - eps &&
           y <= std::max(a.y, b.y) + eps;
}

bool LocationMonitor::point_in_polygon(double x, double y, const std::vector<Point>& polygon) {
    if (polygon.size() < 3) {
        return false;
    }
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& a = polygon[j];
        const auto& b = polygon[i];

        if (point_on_segment(x, y, a, b)) {
            return true;
        }
        bool crosses_y = (a.y > y) != (b.y > y);
        if (!crosses_y) {
            continue;
        }
        double intersection_x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
        if (x < intersection_x) {
            inside = !inside;
        }
    }
    return inside;
}

void LocationMonitor::send_detection_command(const std::string& control, const std::string& algorithm_name, int algorithm_type) {
    nlohmann::json payload = {
        {"cmd", "detection_dynamic_control"},
        {"type", algorithm_name},
        {"switch", control},
        {"params", ""},
    };
    auto send_result = socket_to_perceiver.send(
        zmq::buffer(payload.dump()), 
        zmq::send_flags::dontwait
    );
    if (!send_result) {
        SPDLOG_WARN("通知检测中心发送失败,重建socket: {}", payload.dump());
        rebuild_perceiver_socket();
        return;
    }
    zmq::message_t reply;
    if (!socket_to_perceiver.recv(reply, zmq::recv_flags::none)) {
        SPDLOG_WARN("检测中心回复超时,重建socket");
        rebuild_perceiver_socket();
        return;
    }
    SPDLOG_INFO("检测中心确认: {}", reply.to_string_view());
}

void LocationMonitor::rebuild_perceiver_socket() {
    socket_to_perceiver.close();
    socket_to_perceiver = zmq::socket_t(ctx, zmq::socket_type::req);
    int timeout = kZmqTimeoutMs;
    int linger = kLingerMs;
    int sndhwm = kSndHwm;
    socket_to_perceiver.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_to_perceiver.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket_to_perceiver.connect(kPerceiverEndpoint);
}