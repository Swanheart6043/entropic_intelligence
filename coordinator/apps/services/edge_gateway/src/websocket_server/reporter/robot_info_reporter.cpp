#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "common/websocket_manager.hpp"
#include "db_settings.hpp"
#include "robot_info_reporter.hpp"

void report_robot_info(nlohmann::json& robot_info, std::mutex& robot_info_mtx) {
    using namespace nlohmann;

    std::thread summary_thread = std::thread([&]{
        zmq::context_t context(1);
        zmq::socket_t subscriber(context, ZMQ_SUB);
        subscriber.setsockopt(ZMQ_SUBSCRIBE, "", 0);
        subscriber.connect("tcp://127.0.0.1:6060");

        while (true) {
            try {
                zmq::message_t topic;
                zmq::message_t zmq_message;
                auto topic_result = subscriber.recv(topic, zmq::recv_flags::none);
                if (!topic_result) continue;
                auto message_result = subscriber.recv(zmq_message, zmq::recv_flags::none);
                if (!message_result) continue;
                std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());

                nlohmann::json data = json::parse(message);
                {
                    std::lock_guard<std::mutex> lk(robot_info_mtx);
                    
                    int battery_percentage = data.value("battery_percentage", 0);
                    if (battery_percentage) {
                        robot_info.at("power") = battery_percentage;
                        robot_info.at("powerText") = data.at("battery_percentage_text");
                    }
                    if (data.contains("is_stop")) {
                        robot_info.at("isStop") = data.at("is_stop").get<bool>();
                    }
                    if (data.contains("is_auto_charge")) {
                        robot_info.at("isCharging") = data.at("is_auto_charge").get<bool>();
                    }
                    if (data.contains("temperature")) {
                        robot_info.at("temperature") = data.at("temperature").get<double>();
                        robot_info.at("temperatureText") = data.at("temperature_text").get<std::string>();
                    }
                    if (data.contains("humidity")) {
                        robot_info.at("humidity") = data.at("humidity").get<double>();
                        robot_info.at("humidityText") = data.at("humidity_text").get<std::string>();
                    }
                    if (data.contains("pm2_5")) { 
                        robot_info.at("pm2.5") = data.at("pm2_5").get<double>();
                    }
                    if (data.contains("pm2_5_text")) { 
                        robot_info.at("pm2.5Text") = data.at("pm2_5_text").get<std::string>();
                    }
                    if (data.contains("total_run_time")) {
                        robot_info.at("totalRuntime") = data.at("total_run_time").get<double>();
                    }
                    if (data.contains("current_mileage_m")) { 
                        robot_info.at("curMilleageM") = data.at("current_mileage_m").get<double>();
                    }
                    if (data.contains("current_mileage_m_text")) {
                        robot_info.at("curMilleageMText") = data.at("current_mileage_m_text").get<std::string>();
                    }
                    if (data.contains("current_mileage_km")) {
                        robot_info.at("curMilleageKM") = data.at("current_mileage_km").get<double>();
                    }
                    if (data.contains("current_mileage_km_text")) {
                        robot_info.at("curMilleageKMText") = data.at("current_mileage_km_text").get<std::string>();
                    }
                    if (data.contains("total_mileage_km")) {
                        robot_info.at("totalMileage") = data.at("total_mileage_km").get<double>();
                        robot_info.at("totalMileageText") = data.at("total_mileage_km_text").get<std::string>();
                    }
                    if (data.contains("longitude") && data.contains("latitude")) {
                        nlohmann::json gps_data;
                        
                        // 提取各个字段 (使用 value 提供默认值，防止由于丢包导致程序崩溃)
                        gps_data["valid"] = data.value("valid", 0);
                        gps_data["ew"] = data.value("ew", 0);
                        gps_data["sn"] = data.value("sn", 0);
                        
                        // 注意发送端的注释：经纬度/海拔/速度/方向均为缩放整数，前端负责还原
                        // 如果你决定在这里保持原样交给前端，可以直接取 int：
                        gps_data["longitude"] = data.value("longitude", 0);
                        gps_data["latitude"] = data.value("latitude", 0);
                        gps_data["asl"] = data.value("asl", 0);
                        gps_data["speed"] = data.value("speed", 0);
                        gps_data["direction"] = data.value("direction", 0);

                        // 存入 robot_info
                        robot_info.at("gpsInfo") = gps_data;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            } catch (const std::exception& e) {
                SPDLOG_ERROR("订阅消息异常: {}", e.what());
            }
        }
    });

    std::thread send_thread = std::thread([&]{
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            try {
                auto settings = DBSettings::get_all();
                nlohmann::json response = {{"command", "device-info"}, {"content", ""}};
                {
                    std::lock_guard<std::mutex> lk(robot_info_mtx);
                    robot_info.at("isHeadLightOpen") = (settings["head_light"] == "on");
                    robot_info.at("isWarningLightOpen") = (settings["warning_light"] == "on");
                    robot_info.at("isWipersOpen") = (settings["continuous_wiper"] == "on");
                    robot_info.at("wipersMode") = settings["continuous_wiper"] == "on" ? "continuous" : "single";
                    robot_info.at("volume") = settings["volume"];
                    response.at("content") = robot_info;
                }
                WebSocketManager::getInstance().broadcast(response.dump());
            } catch (const std::exception& e) {
                SPDLOG_ERROR("上传机器人信息异常: {}", e.what());
            }
        }
    });

    send_thread.join();
    summary_thread.join();
}