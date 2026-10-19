#include <utility>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "robot_task/manual/manual_motion_executor.hpp"
#include "initialization_handler.hpp"

InitializationController::InitializationController() {
    http = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
}

void InitializationController::initialize_un_wait(const std::string& message, zmq::socket_t& zmq_socket) {
    try {  
        using namespace nlohmann;
        
        auto pointInfo = json::parse(message).at("PointInfo");
        auto point = pointInfo.at("Point");
        json params = {
            {"mapName", pointInfo.at("MapName")},
            {"point", {
                {"angle", std::stoi(point.value("Angle", ""))},
                {"gridPosition", {
                    {"x", std::stoi(point.value("PointX", ""))}, 
                    {"y", std::stoi(point.value("PointY", ""))}
                }}
            }}
        };
		json answer = {
            {"Header", {
                { "CmdType", "RC_Robot_Initialize_Customized_Directly" },
                { "Sn", config->get_sn() },
                { "Time", std::time(nullptr) }
            }},
            {"Result", 1}, {"Data", ""}
        };
		zmq_socket.send(zmq::buffer(answer.dump()), zmq::send_flags::dontwait);

        auto response = http->Post(
            base_url + "/cmd/initialize_customized_directly", 
            params.dump(), 
            "application/json"
        );
        if (!response || response->status != 200) {
			SPDLOG_INFO("不转圈初始化失败: {}", params.dump());
            return;
        }
        
		SPDLOG_INFO("不转圈初始化完成");
    } catch (const std::exception &e) {
		SPDLOG_ERROR("不转圈初始化错误: {}", e.what());
    }
}

void InitializationController::initialize_wait(const std::string& message, zmq::socket_t& zmq_socket) {
    try {
        using namespace nlohmann;

        auto pointInfo = json::parse(message).at("PointInfo");
        auto point = pointInfo.at("Point");
		json answer = {
            {"Header", {
                {"CmdType", "RC_Robot_Initialize_Customized"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        }; 
        zmq_socket.send(zmq::buffer(answer.dump()), zmq::send_flags::dontwait);

        ManualMotionExecutor::Task task;
        task.kind = ManualMotionExecutor::Kind::INITIALIZE_WAIT;
        task.map_name = pointInfo.at("MapName").get<std::string>();
        task.angle = std::stod(point.at("Angle").get<std::string>());
        task.x = std::stod(point.at("PointX").get<std::string>());
        task.y = std::stod(point.at("PointY").get<std::string>());
        ManualMotionExecutor::instance().submit(std::move(task));

    } catch (const std::exception &e) {
		SPDLOG_ERROR("转圈初始化异常: {}", e.what());
    }
}

void InitializationController::initialize_check_finish(zmq::socket_t& zmq_socket) {
    try {
        using namespace nlohmann;
		
		auto response = http->Get(base_url + "/cmd/is_initialize_finished");
        json answer = {
			{"Header", {
				{"CmdType", "RC_Robot_Is_Initialize_Finished"}, 
				{"Sn", config->get_sn()},
				{"Time", std::time(nullptr)}
			}},
			{"Result", 1}, {"Data", ""}
		};
        json answer1 = {
            {"Header", {
                { "CmdType", "RC_INIT_RET" },
                { "Sn", config->get_sn() },
                { "Time", std::time(nullptr) }
            }},
            {"Result", 1}, {"Data", response->status == 200 ? "6" : "-1"}
        };
        zmq_socket.send(zmq::buffer(answer.dump()), zmq::send_flags::dontwait);
        zmq_socket.send(zmq::buffer(answer1.dump()), zmq::send_flags::dontwait);
		
        SPDLOG_INFO("初始化完成!");

	} catch (const std::exception& e) {
		SPDLOG_ERROR("初始化异常: {}", e.what());
    }
}

void InitializationController::initialize_stop(zmq::socket_t& zmq_socket) {
    try {
        using namespace nlohmann;
		json answer = {
			{"Header", {
				{"CmdType", "RC_Robot_Stop_Initialize"}, 
				{"Sn", config->get_sn()},
				{"Time", std::time(nullptr)}
			}},
			{"Result", 1}, {"Data", ""}
		};
		ManualMotionExecutor::instance().cancel_current();
		auto response = http->Get(base_url + "/cmd/stop_initialize");    
		if (!response || response->status != 200) {            
			answer.at("Result") = 0;
			zmq_socket.send(zmq::buffer(answer.dump()), zmq::send_flags::dontwait);
			return;
		}
		zmq_socket.send(zmq::buffer(answer.dump()), zmq::send_flags::dontwait);

		SPDLOG_INFO("停止初始化完成!");
	} catch (const std::exception& e) {
		SPDLOG_ERROR("停止初始化异常: {}", e.what());
    }
}
