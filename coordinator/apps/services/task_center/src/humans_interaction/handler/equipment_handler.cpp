#include <iostream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "component/ptz_controller.hpp"
#include "component/mcu_controller.hpp"
#include "equipment_handler.hpp"

EquipmentHandler::EquipmentHandler(zmq::socket_t& zmq_socket) : zmq_socket_(zmq_socket) {
    
}

void EquipmentHandler::handle(const std::string& message) {
    try {
        using namespace nlohmann;

        json ack = {
            {"Header", {{"CmdType", "RC_RobotEnable_Set"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
        
        auto which = json::parse(message).at("Switch").at("Type");
        // 大灯, 警灯
        if (which == "14" || which == "12") {
            handle_lights(message);
            return;
        }
        // 雨刮器
        if (which == "23") {
           handle_wipers(message);
           return;
        }
	} catch (const std::exception& e) {
		SPDLOG_ERROR("控制灯光异常: {}", e.what());
	}
}

void EquipmentHandler::handle_lights(const std::string& message) {
    using namespace nlohmann;
    auto which = json::parse(message).at("Switch").at("Type");
    auto value = json::parse(message).at("Switch").at("Value");

    McuController mcu_controller;
    // 大灯
    if (which == "14") {
        if (value == "1") {
            mcu_controller.turn_on_head_light();
            return;
        }
        if (value == "0") {
            mcu_controller.turn_off_head_light();
            return;
        }
    }
    // 警灯
    if (which == "12") {
        if (value == "1") {
            mcu_controller.turn_on_warning_light();
            return;
        }
        if (value == "0") {
            mcu_controller.turn_off_warning_light();
            return;
        }
    }
}

void EquipmentHandler::handle_wipers(const std::string& message) {
    using namespace nlohmann;
    auto value = json::parse(message).at("Switch").at("Value");

    PTZController ptz_controller;
    if (value == "1") {
        ptz_controller.turn_on_wiper(WiperMode::SINGLE_BRUSH);
        ptz_controller.turn_off_wiper(WiperMode::OFF);
        return;
    }
    if (value == "2") {
        ptz_controller.turn_on_wiper(WiperMode::CONTINUOUS_BRUSH);
        return;
    }
    if (value == "0") {
        ptz_controller.turn_off_wiper(WiperMode::OFF);
        return;
    }
}

void EquipmentHandler::release_emergency_stop() {
    try {
        nlohmann::json ack = {
            {"Header", {{"CmdType", "RC_Robot_Release_Scram"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1}, {"Data", ""}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

        McuController mcu_controller;
        mcu_controller.release_emergency_stop();
    } catch (const std::exception &e) {
		SPDLOG_ERROR("解除急停异常: {}", e.what());
    }
}

void EquipmentHandler::match_reboot_operate(const std::string& message) {
    std::string type = nlohmann::json::parse(message).value("RestartType", "");

    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "RC_Robot_Restart_Ctrl"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    zmq_socket_.send(
        zmq::buffer(ack.dump()),
        zmq::send_flags::dontwait
    );

    if (type == "0" || type == "1") {
        McuController mcu_controller;
        mcu_controller.reboot_power();
    }
    if (type == "2") {
        McuController mcu_controller;
        mcu_controller.reboot_industrial_computer();
    }
    if (type == "3") {
        McuController mcu_controller;
        mcu_controller.reboot_navigation();
    }
    if (type == "4") {
        McuController mcu_controller;
        mcu_controller.shutdown();
    }
}