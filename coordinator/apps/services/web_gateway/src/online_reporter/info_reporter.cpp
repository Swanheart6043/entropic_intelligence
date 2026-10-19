#include <iostream>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "common/wss_connector.hpp"
#include "info_reporter.hpp"
#include "common/reporter_logger.hpp"

InfoReporter::InfoReporter(WebSocketClient& websocket_connect) : websocket_connect_(websocket_connect) {
    
}

InfoReporter::~InfoReporter() {
    stop();
}

void InfoReporter::start() {
    if (report_thread_.joinable()) {
        auto logger = spdlog::get("web_gateway");
        SPDLOG_LOGGER_WARN(logger, "上线报告已在运行中");
        return;
    }
    
    should_stop_ = false;
    report_thread_ = std::thread(&InfoReporter::report_loop, this);
}

void InfoReporter::stop() {
    should_stop_ = true;
    cv_.notify_all();
    
    if (report_thread_.joinable()) {
        report_thread_.join();
    }
}

void InfoReporter::report_loop() {
    using namespace nlohmann;
    auto logger = spdlog::get("web_gateway");
    Config* config = Config::getInstance();

    json robot_info = {
        {"Header", {
            {"CmdType", "MCUStatusPost"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {
            {"ChargeConnectMode", 0}, // 充电状态
            {"ChargeStat", 0}, // 充电状态（0未充电，1正在充电，2充满）
            {"SOC", "0"}, // 电量
            {"ScramBut", 0}, // 急停按钮状态（0未按下，1已按下）
            {"CurRuntime", 0}, // 当前运行时间
            {"CurMilleage", 0}, // 当前里程
            {"TotalRuntime", 0}, // 累计运行时间
            {"TotalMilleage", 0}, // 累计里程
            {"ExternalSensor", { // 传感器
                {"SensorHumidity", "0"}, // 湿度
                {"SensorTemp", "0"}, // 温度
                {"pmOne", "0"}, // pm1.0
                {"pmTwo", "0"}, // pm2.5
            }},
            {"Voltage", "0"}, // 电压
            {"Current", "0"}, // 电流
            {"BatteryTemp", "0"}, // 电池温度
            {"PowerAnomaly", "0"}, // 电源异常标志（0正常，1异常）
            {"CameraZoom", 0}, // 摄像头变焦倍数
            {"CameraFocus", 0}, // 摄像头焦距 
            {"PTZHorizontalPosition", "28.3"}, // 云台水平角度（单位°）
            {"PTZVerticalPosition", "1"}, // 云台垂直角度（单位°）
            {"CameraLightStatus", 1}, // 摄像头补光灯状态（0关闭，1开启）
            {"Ultrasonic", nlohmann::json::array()}, // 超声波传感器数组
        }}
    };
    std::mutex robot_info_mtx;
    
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
                auto zmq_message_result = subscriber.recv(zmq_message, zmq::recv_flags::none);
                if (!zmq_message_result) continue;

                std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
                nlohmann::json data = json::parse(message);
                {
                    std::lock_guard<std::mutex> lk(robot_info_mtx);

                    if (data.contains("battery_percentage")) {
                        int battery_percentage = data.at("battery_percentage").get<int>();
                        robot_info.at("MCUStatus").at("SOC") = std::to_string(battery_percentage);
                    }

                    if (data.contains("is_stop")) {
                        bool is_stop = data.at("is_stop").get<bool>();
                        robot_info.at("MCUStatus").at("ScramBut") = is_stop ? 1 : 0;
                    }
                    
                    if (data.contains("is_auto_charge")) {
                        bool is_auto_charge = data.at("is_auto_charge").get<bool>();
                        robot_info.at("MCUStatus").at("ChargeStat") = is_auto_charge ? 1 : 0;
                    }
                    
                    if (data.contains("temperature")) {
                        double temperature = data.at("temperature").get<double>();
                        robot_info.at("MCUStatus").at("ExternalSensor").at("SensorTemp") = std::to_string(temperature);
                    }
                    
                    if (data.contains("humidity")) {
                        double humidity = data.at("humidity").get<double>();
                        robot_info.at("MCUStatus").at("ExternalSensor").at("SensorHumidity") = std::to_string(humidity);
                    }
                    
                    if (data.contains("pm2_5")) {
                        int pm2_5 = data.at("pm2_5").get<int>();
                        robot_info.at("MCUStatus").at("ExternalSensor").at("pmTwo") = std::to_string(pm2_5);
                    }
                    
                    if (data.contains("pm1_0")) {
                        int pm1_0 = data.at("pm1_0").get<int>();
                        robot_info.at("MCUStatus").at("ExternalSensor").at("pmOne") = std::to_string(pm1_0);
                    }

                    if (data.contains("total_mileage_m")) {
                        int total_mileage_m = data.at("total_mileage_m").get<int>();
                        robot_info.at("MCUStatus").at("TotalMilleage") = std::to_string(total_mileage_m);
                    }

                    if (data.contains("total_run_time")) {
                        int total_run_time = data.at("total_run_time").get<int>();
                        robot_info.at("MCUStatus").at("TotalRuntime") = std::to_string(total_run_time);
                    }

                    if (data.contains("current_mileage_m")) {
                        int current_mileage_m = data.at("current_mileage_m").get<int>();
                        robot_info.at("MCUStatus").at("CurMilleage") = std::to_string(current_mileage_m);
                    }

                    if (data.contains("current_run_time")) {
                        int current_run_time = data.at("current_run_time").get<int>();
                        robot_info.at("MCUStatus").at("CurRuntime") = std::to_string(current_run_time);
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
            } catch (const std::exception& e) {
                SPDLOG_LOGGER_ERROR(logger, "订阅消息异常: {}", e.what());
            }
        }
    });

    std::thread send_thread = std::thread([&]{
        while (true) {
            try {
                std::this_thread::sleep_for(std::chrono::seconds(10));
                std::string payload;
                {
                    std::lock_guard<std::mutex> lk(robot_info_mtx);
                    payload = robot_info.dump();
                }
                websocket_connect_.send_message(payload);
                REPORTER_LOG_INFO("机器人信息: {}", payload);
            } catch (const std::exception& e) {
                SPDLOG_LOGGER_ERROR(logger, "上传机器人信息异常: {}", e.what());
            }
        }
    });

    send_thread.join();
    summary_thread.join();
}