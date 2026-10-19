#include <ctime>
#include <iostream>
#include <utility>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "mode/mode_service.hpp"
#include "robot_task/manual/manual_motion_executor.hpp"
#include "navigation_handler.hpp"

NavigationHandler::NavigationHandler(zmq::socket_t& zmq_socket) : zmq_socket_(zmq_socket) {
}

NavigationHandler::~NavigationHandler() {
}

void NavigationHandler::move(const std::string& message) {
    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "RC_RobotRun_Ctrl"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

    if (ModeService::instance().is_auto_mode()) {
        SPDLOG_WARN("自动模式下, 不处理移动命令");
        return;
    }
    
    try {
        using namespace nlohmann;
		nlohmann::json message_json_obj = json::parse(message);
		auto move_data = message_json_obj.at("Move");
		if (move_data.empty()) {
			SPDLOG_ERROR("Move字段不能为空");
			return;
		}

		std::string linearSpeed = move_data.value("LinearSpeed", "");
		std::string angularSpeed = move_data.value("AngularSpeed", "");
		ManualMotionExecutor::Task task;
        task.kind = ManualMotionExecutor::Kind::MOVE;
        task.linear_speed = std::stod(linearSpeed);
        task.angular_speed = std::stod(angularSpeed);
        ManualMotionExecutor::instance().submit(std::move(task));

    } catch (const std::exception& e) {
		SPDLOG_ERROR("Move异常: {}", e.what());
    }
}

void NavigationHandler::go_to_point(const std::string& message) {
    try {
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_Robot_Navigate_To_Point"}, 
                {"Sn", config->get_sn()}, 
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

        if (ModeService::instance().is_auto_mode()) {
            SPDLOG_WARN("自动模式下, 不处理导航到点指令");
            return;
        }
        auto point_info = nlohmann::json::parse(message).at("Point_Info");
        ManualMotionExecutor::Task task;
        task.kind = ManualMotionExecutor::Kind::GO_TO_POINT;
        task.map_name = point_info.at("MapName").get<std::string>();
        task.x_text = point_info.at("PointX").get<std::string>();
        task.y_text = point_info.at("PointY").get<std::string>();
        task.angle_text = point_info.at("Angle").get<std::string>();
        task.x = std::stod(task.x_text);
        task.y = std::stod(task.y_text);
        task.angle = std::stod(task.angle_text);
        ManualMotionExecutor::instance().submit(std::move(task));
    } catch (const std::exception& e) {
        SPDLOG_ERROR("导航到点异常: {}", e.what());
    }
}

void NavigationHandler::control_to_point(const std::string& message) {
    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "RC_Robot_Navigate_Execution_Control"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

    if (ModeService::instance().is_auto_mode()) {
        SPDLOG_WARN("自动模式下, 不处理暂停、恢复导航到点指令");
        return;
    }
    nlohmann::json type = nlohmann::json::parse(message).at("Navigate_Execution_Control");
    if (type == "1") {
        ManualMotionExecutor::instance().pause_current();
        return;
    }
    if (type == "2") {
        ManualMotionExecutor::instance().resume_current();
        return;
    }
}

void NavigationHandler::run_line(const std::string& message) {
    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "NB_RobotRunOnce_Laser_Set"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

    if (ModeService::instance().is_auto_mode()) {
        SPDLOG_WARN("自动模式下忽略跑线指令");
        return;
    }

    try {
		using namespace nlohmann;
		
		json patrol_list = json::parse(message).at("PatrolList");
		if (patrol_list.empty()) {
			std::cout << "patrol_list不能为空" << std::endl;
			return;
		}
		json patrol_info = patrol_list.at("PatrolInfo");
		if (patrol_info.empty()) {
			return;
		}		
		json map_name = patrol_list.at("MapName");
		if (map_name.empty()) {
			return;
		}

        ManualMotionExecutor::Task task;
        task.kind = ManualMotionExecutor::Kind::RUN_LINE;
        task.map_name = map_name.get<std::string>();
        ManualMotionExecutor::instance().submit(std::move(task));
	} catch (const std::exception& e) {
		SPDLOG_ERROR("跑线异常: {}", e.what());
	}
}
