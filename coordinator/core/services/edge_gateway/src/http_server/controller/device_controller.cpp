#include <iostream>
#include <unordered_set>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "db_connection.hpp"
#include "../common/http_helpers.hpp"
#include "device_controller.hpp"

DeviceInfoController::DeviceInfoController() {
    socket_to_task_center.connect("tcp://127.0.0.1:4004");
}

DeviceInfoController::~DeviceInfoController() {
    
}

std::string DeviceInfoController::change_mode(const httplib::Request& req) {    
    try {
        // 解析参数
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string mode = body.value("mode", ""); // auto 或 manual
        if (mode.empty()) return fail(400, i18n::Msg::InvalidParameter);

        // 处理业务
        nlohmann::json message = {
            {"Header", {{"CmdType", "RC_RobotCtrlMode_Set"}, {"To", "Sn"}, {"CmdAttribute", "Post"}}},
            {"Mode", {{"unCtrlMode", mode == "manual" ? "1" : "0"}, {"unIsContinue", "0"}}}
        };
        socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

        // 返回响应
        return success();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("切换模式异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

std::string DeviceInfoController::charging(const httplib::Request& req) {
    try {
        // 解析参数
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string type = body.value("type", "");  // charging 或 cancel
        if (type.empty()) return fail(400, i18n::Msg::InvalidParameter);

        // 处理业务
        nlohmann::json message = {
            {"Header", {{"CmdType", "RC_Robot_Ctrl"}, {"To", "Sn"}, {"CmdAttribute", "Post"}}},
            {"Robot_Ctrl", {{"unCtrlType", "2"}, {"unCtrlValue", type == "charging" ? "1" : "0"}}}
        };
        socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
        
        // 返回响应
        return success();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("操作充电异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

std::string DeviceInfoController::control_light(const httplib::Request& req) {
    try {
        // 解析参数
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string type = body.value("type", "");          // headLight 或 warningLight
        std::string switch_text = body.value("switch", "");  // on 或 off
        if (type.empty() || switch_text.empty()) return fail(400, i18n::Msg::InvalidParameter);

        // 处理业务
        nlohmann::json message = {
            {"Header", {{"CmdType", "RC_RobotEnable_Set"}}},
            {"Switch", {{"Type", type == "headLight" ? "14" : "12"}, {"Value", switch_text == "on" ? "1" : "0"}}}
        };
        socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

        // 返回响应
        return success();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("控制灯光异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

std::string DeviceInfoController::control_ptz(const httplib::Request& req) {
    try {
        // 解析参数
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string direction = body.value("direction", "");
        if (direction.empty()) return fail(400, i18n::Msg::InvalidParameter);

        // 处理业务
        nlohmann::json message = {
            {"Header", {{"CmdType", "RC_Robot_PTZ_Ctrl"}}},
            {"Robot_PTZ_Ctrl", {{"unCtrlValue", "0"},{"Value", "0"}}}
        };
        if (direction == "up") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "7";
        }
        if (direction == "down") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "8";
        }
        if (direction == "left") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "5";
        }
        if (direction == "right") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "6";
        }
        if (direction == "stop") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "0";
        }
        if (direction == "reset") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "12";
            message.at("Robot_PTZ_Ctrl").at("Value") = "12";
        }
        if (direction == "zoomIn") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "9";
        }
        if (direction == "zoomOut") {
            message.at("Robot_PTZ_Ctrl").at("unCtrlValue") = "10";
        }
        socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

        // 返回响应
        return success();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("控制云台异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

std::string DeviceInfoController::control_wiper(const httplib::Request& req) {
    try {
        // 解析参数
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string type = body.value("type", ""); // single 或 continuous
        std::string wiper_switch = body.value("switch", ""); // on 或 off，单刷时可以不传
        if (wiper_switch.empty()) return fail(400, i18n::Msg::InvalidParameter);

        // 处理业务
        nlohmann::json message = {
            {"Header", {{"CmdType", "RC_RobotEnable_Set"}}},
            {"Switch", {{"Type", "23"}, {"Value", "1"}}}
        };
        if (wiper_switch == "on") {
            if (type == "single") {
                message.at("Switch").at("Value") = "1";
                socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
            }
            if (type == "continuous") {
                message.at("Switch").at("Value") = "2";
                socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
            }
        }
        if (wiper_switch == "off") {
            message.at("Switch").at("Value") = "0";
            socket_to_task_center.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
        }

        // 返回响应
        return success();
    } catch (const std::exception& e) {
        SPDLOG_ERROR("控制雨刷异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

// 保存边缘所有硬件的版本信息 - 低频请求
nlohmann::json DeviceInfoController::save_edge_hardware_version(const httplib::Request& req) {
    // 解析参数
    nlohmann::json body = nlohmann::json::parse(req.body);
    std::string hw_type = body.value("hardwareType", "");
    std::string version = body.value("version", "");
    
    if (hw_type.empty() || version.empty()) {
        SPDLOG_INFO("设置版本时，参数不合法");
        return fail(400, i18n::Msg::InvalidParameter);
    }
    static const std::unordered_set<std::string> kValidHwTypes = {
        "screen", "coordinator", "control", "perceiver", "cognition"
    };
    if (kValidHwTypes.find(hw_type) == kValidHwTypes.end()) {
        SPDLOG_INFO("设置版本时, hardwareType 不在白名单: {}", hw_type);
        return fail(400, i18n::Msg::InvalidParameter);
    }

    // 处理业务：UPSERT，首次插入 / 已存在则更新版本号
    auto* conn = DBConnection::get_instance().get_connection();
    std::string sql =
        "INSERT INTO device_versions (device_type, sw_version) VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE sw_version = VALUES(sw_version)";
    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
    pstmt->setString(1, hw_type);
    pstmt->setString(2, version);
    pstmt->executeUpdate();
    SPDLOG_INFO("保存边缘硬件版本成功: {} -> {}", hw_type, version);

    // 返回响应
    return nlohmann::json::object();
}

// 查询边缘所有硬件的版本信息 - 低频请求
std::string DeviceInfoController::query_edge_hardware_version(const httplib::Request& req) {
    try {
        // 总结构
        nlohmann::json edge_version_info = {
            {"navigationVersion", "0.0.0"},
            {"controlVersion", "0.0.0"},
            {"coordinatorVersion", "0.0.0"},
            {"perceiveVersion", "0.0.0"},
            {"cognitionVersion", "0.0.0"},
            {"screenVersion", "0.0.0"},
            {"ptzVersion", "0.0.0"},
        };
        
        // 一次性读取 device_versions 表，构建 device_type -> sw_version 映射
        std::unordered_map<std::string, std::string> versions;
        {
            auto* conn = DBConnection::get_instance().get_connection();
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            std::unique_ptr<sql::ResultSet> res(
                stmt->executeQuery("SELECT device_type, sw_version FROM device_versions")
            );
            while (res->next()) {
                versions[res->getString("device_type")] = res->getString("sw_version");
            }
        }
        // 用查询结果填充存在的字段（查不到则保留默认值）
        auto fill = [&](const std::string& device_type, const char* json_key) {
            auto it = versions.find(device_type);
            if (it != versions.end()) {
                edge_version_info.at(json_key) = it->second;
            } else {
                SPDLOG_WARN("device_versions 表中缺少 {} 的版本记录", device_type);
            }
        };
        
        fill("coordinator", "coordinatorVersion");
        fill("control", "controlVersion");
        fill("screen", "screenVersion");
        fill("perceiver", "perceiveVersion");
        fill("navigation", "navigationVersion");
        
        SPDLOG_INFO("发送版本信息成功: {}", edge_version_info.dump());
        return http_helpers::ok_body(std::move(edge_version_info), i18n::Msg::QuerySuccess);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("获取边缘硬件版本异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    } 
}

// 查询算法类型 - 低频请求
std::string DeviceInfoController::query_algorithm_type(const httplib::Request& req) {
    try {
        // 处理业务
        nlohmann::json target_detection_list = {
            {{"name", "smoking"}, {"remark", "吸烟检测"}},
            {{"name", "face"}, {"remark", "人脸检测"}},
            {{"name", "licensePlate"}, {"remark", "车牌检测"}},
            {{"name", "helmet"}, {"remark", "安全帽检测"}},
            {{"name", "reflectiveVest"}, {"remark", "反光衣检测"}},
            {{"name", "infraredHeat"}, {"remark", "红外高温检测"}},
            {{"name", "fireSmoke"}, {"remark", "火灾浓烟检测"}},
            {{"name", "breaking_into_penalty_area"}, {"remark", "禁区闯入检测"}},
        };

        // 返回响应
        return http_helpers::ok_body(std::move(target_detection_list), i18n::Msg::QuerySuccess);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("获取算法区域异常: {}", e.what());
        return fail(500, i18n::Msg::InternalError);
    }
}

// 保存算法区域 - 低频请求
nlohmann::json DeviceInfoController::save_algorithm_area(const httplib::Request& req) {
    // 解析参数
    nlohmann::json body = nlohmann::json::parse(req.body);
    std::string map_name = body.value("mapName", "");
    
    // 检查参数：确保 mapName 存在，且 algorithmAreas 是个数组
    if (map_name.empty() || !body.contains("algorithmAreas") || !body["algorithmAreas"].is_array()) {
        SPDLOG_WARN("保存算法区域时，参数不合法: {}", req.body);
        return fail(400, i18n::Msg::InvalidParameter);
    }
    
    // 将 algorithmAreas 数组转为字符串以便存入数据库
    std::string area_list_str = body["algorithmAreas"].dump();

    // 处理业务
    auto* conn = DBConnection::get_instance().get_connection();
    
    // 核心：利用 UNIQUE 约束，存在则更新，不存在则插入
    std::string sql = 
        "INSERT INTO algorithm_areas (map_name, area_data) "
        "VALUES (?, ?) "
        "ON DUPLICATE KEY UPDATE area_data = VALUES(area_data)";
        
    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
    pstmt->setString(1, map_name);
    pstmt->setString(2, area_list_str);
    pstmt->executeUpdate();

    // 返回响应（假设你有一套封装好的返回体格式）
    SPDLOG_INFO("保存成功");
    return nlohmann::json::object();
}

// 查询算法区域 - 低频请求
std::string DeviceInfoController::query_algorithm_area(const httplib::Request& req) {
    std::string map_name;
        
    // 适配 GET 请求参数获取 (如 /api/query?mapName=gs1225)
    if (req.has_param("mapName")) {
        map_name = req.get_param_value("mapName");
    } 
    // 兼容 POST 请求 body 传参方式
    else if (!req.body.empty()) {
        nlohmann::json body = nlohmann::json::parse(req.body);
        map_name = body.value("mapName", "");
    }

    // 检查参数
    if (map_name.empty()) {
        SPDLOG_WARN("查询算法区域时，缺少 mapName 参数");
        return fail(400, i18n::Msg::InvalidParameter);
    }

    // 处理业务
    auto* conn = DBConnection::get_instance().get_connection();
    std::string sql = "SELECT area_data FROM algorithm_areas WHERE map_name = ?";
    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
    pstmt->setString(1, map_name);
    
    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
    
    // 组装返回数据结构
    nlohmann::json result_data;
    result_data["mapName"] = map_name;
    result_data["algorithmAreas"] = nlohmann::json::array(); // 默认给个空数组，防止前端取值崩溃

    if (res->next()) {
        std::string area_data_str = res->getString("area_data");
        if (!area_data_str.empty()) {
            // 将数据库中的 JSON 字符串反序列化回 JSON 数组
            result_data["algorithmAreas"] = nlohmann::json::parse(area_data_str);
        }
    }

    // 返回响应
    return http_helpers::ok_body(result_data, i18n::Msg::QuerySuccess);
}