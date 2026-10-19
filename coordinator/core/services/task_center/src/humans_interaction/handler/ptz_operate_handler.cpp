#include <future>
#include <iostream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "db_connection.hpp"
#include "component/ptz_controller.hpp"
#include "ptz_operate_handler.hpp"

PtzOperateHandler::PtzOperateHandler(zmq::socket_t& zmq_socket) : zmq_socket_(zmq_socket) {
    
}

void PtzOperateHandler::match_ptz_operate(const std::string& message) {
    auto ptz_ctrl = nlohmann::json::parse(message).at("Robot_PTZ_Ctrl");
    nlohmann::json direction = ptz_ctrl.at("unCtrlValue");
    
    nlohmann::json ack = {
        {"Header", {{"CmdType", "RC_Robot_PTZ_Ctrl"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
        {"Result", 1}, {"Data", ""}
    };
    zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

    // 向左
    if (direction == "5") {
        ptz_controller.to_left();
        return;
    }
    // 向右
    if (direction == "6") {
        ptz_controller.to_right();
        return;
    }
    // 向下
    if (direction == "8") {
        ptz_controller.to_down();
        return;
    }
    // 向上
    if (direction == "7") {
        ptz_controller.to_up();
        return;
    }
    // 停止
    if (direction == "0") {
        ptz_controller.stop();
        return;
    }
    // 放大
    if (direction == "9") {
        ptz_controller.zoom_in();
        return;
    }
    // 缩小
    if (direction == "10") {
        ptz_controller.zoom_out();
        return;
    }
    // 调用预置位
    if (direction == "12") {
        auto value = ptz_ctrl.at("Value").get<std::string>();
        if (value == "12") {
            ptz_controller.zoom_reset();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            ptz_controller.reset_ptz();
            return;
        }
        PTZController preset_position_executor;
        preset_position_executor.execute_preset_position(value);
        return;
    }
}

void PtzOperateHandler::match_preset_position_operate(const std::string& message) {
    auto robot_preset = nlohmann::json::parse(message).at("Robot_Preset");
    std::string type = robot_preset.at("Type").get<std::string>();
    std::string preset_position_code = robot_preset.value("PresetID", "");
    std::string preset_position_name = robot_preset.at("PresetName").get<std::string>();

    if (type == "add") {
        create_preset_position(preset_position_name);
    }
    if (type == "edit") {
        update_preset_position(preset_position_code, preset_position_name);
    }
}

void PtzOperateHandler::create_preset_position(const std::string& name) {
    try {
        auto* conn = DBConnection::get_instance().get_connection();

        // 获取当前最大 preset_position_code
        std::unique_ptr<sql::Statement> stmt(
            conn->createStatement()
        );
        std::unique_ptr<sql::ResultSet> res(
            stmt->executeQuery("SELECT MAX(preset_position_code) FROM preset_position")
        );
        int max_code = 0;
        if (res->next()) {
            max_code = res->getInt(1);
        }

        // 设置新的 preset_position_code
        int new_code = max_code + 1;

        // 获取角度
        double horizontal_angle = 0.00;
        double vertical_angle = 0.00;
        ptz_controller.query_pan(horizontal_angle);
        ptz_controller.query_tilt(vertical_angle);

        // 构建SQL, 设置所有参数
        std::string sql = "INSERT INTO preset_position (preset_position_code, preset_position_name, horizontal_angle, vertical_angle) VALUES (?, ?, ?, ?)";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setInt(1, new_code);
        pstmt->setString(2, name);
        pstmt->setDouble(3, horizontal_angle);
        pstmt->setDouble(4, vertical_angle);

        // 执行插入
        pstmt->executeUpdate();
        SPDLOG_INFO("新增预置位成功");

        // 发送回执
        using namespace nlohmann;
        json ack = {
            {"Header", {
                {"CmdType", "RC_Robot_Preset_Set"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", {{"PresetID", std::to_string(new_code)}}}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("新增预置位, 数据库错误: {}", e.what());
    } catch (std::exception &e) {
        SPDLOG_ERROR("新增预置位, 发生异常: {}", e.what());
    }
}

void PtzOperateHandler::update_preset_position(const std::string& code, const std::string& name) {
    try {
        auto* conn = DBConnection::get_instance().get_connection();

        // 获取角度
        double horizontal_angle = 0.00;
        double vertical_angle = 0.00;
        ptz_controller.query_pan(horizontal_angle);
        ptz_controller.query_tilt(vertical_angle);

        // 构建SQL
        std::string sql = "UPDATE preset_position SET preset_position_name = ?, horizontal_angle = ?, vertical_angle = ? WHERE preset_position_code = ?";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setString(1, name);
        pstmt->setDouble(2, horizontal_angle);
        pstmt->setDouble(3, vertical_angle);
        pstmt->setString(4, code);

        // 执行更新
        pstmt->executeUpdate();
        SPDLOG_INFO("编辑预置位成功");

        // 发送回执
        using namespace nlohmann;
        json ack = {
            {"Header", {{"CmdType", "RC_Robot_Preset_Set"},{"Sn", config->get_sn()},{"Time", std::time(nullptr)}}},
            {"Result", 1},
            {"Data", {{"PresetID", code},{"PresetName", name}}}
        };
        zmq_socket_.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("编辑预置位, 数据库错误: {}", e.what());
    } catch (std::exception &e) {
        SPDLOG_ERROR("编辑预置位, 发生异常: {}", e.what());
    }
}