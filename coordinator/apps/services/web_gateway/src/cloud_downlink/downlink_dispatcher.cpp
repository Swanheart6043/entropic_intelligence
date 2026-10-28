#include <iostream>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "downlink_dispatcher.hpp"

CloudDownlinkDispatcher::CloudDownlinkDispatcher(WebSocketClient& websocket_connect) : websocket(websocket_connect) {
    socket_to_task_center.connect("ipc://127.0.0.1:4004");
    socket_to_edge_gateway.connect("ipc://@from_web_gateway");
}

CloudDownlinkDispatcher::~CloudDownlinkDispatcher() {
    stop();
}

void CloudDownlinkDispatcher::start() {
    if (worker_thread.joinable()) {
        SPDLOG_WARN("接收线程已在运行中");
        return;
    }
    extractor_handler.start();
    worker_thread = std::thread(&CloudDownlinkDispatcher::listen_platform, this);
}

void CloudDownlinkDispatcher::stop() {
    extractor_handler.stop();
    if (worker_thread.joinable()) worker_thread.join();
}

void CloudDownlinkDispatcher::listen_platform() {
    websocket.set_message_callback([&](const std::string& message) {
        SPDLOG_INFO("收到云端命令: {}", message);
        std::string command = nlohmann::json::parse(message).at("Header").at("CmdType");
        try {
            // 同步地图信息
            if (command == "RC_Robot_Map_Info_Get") {
                map_operate_handler.synchronous_base_info(websocket);
                return;
            }
            // 删除地图 - 低频
            if (command == "RC_Robot_Map_Delete") {
                map_operate_handler.remove_map(websocket, message);
                return;
            }
            // 同步预置位 - 低频
            if (command == "RC_Robot_Preset_Get") {
                map_operate_handler.read(websocket);
                return;
            }
            // 删除预置位 - 低频
            if (command == "RC_Robot_Preset_Delete") {
                map_operate_handler.remove(websocket, message);
                return;
            }
            // 保存动作组 - 低频
            if (command == "RC_RobotAction_Set") {
                patrol_plan_handler.save_action_group(websocket, message);
                return;
            }
            // 保存巡逻点 - 低频
            if (command == "NB_RobotPatrol_Laser_Set") {
                patrol_plan_handler.save_patrol_point(websocket, message);
                return;
            }
            // 视频推流 - 低频
            if (command == "PTZ_Video_Url_Get") {
                AudioController video_controller;
                video_controller.notification_streaming(websocket, message);
                return;
            }
            // 人员数据下发 - 低频
            if (command == "RC_Robot_Register_Face_Update_Notice") {
                FaceDatabaseHandler face_database_handler;
                face_database_handler.handle(websocket, message);
                return;
            }
            // 保存巡逻计划 - 低频
            if (command == "NB_RobotPatrolPlan_Set") {
                patrol_plan_handler.save_patrol_plan(websocket, message);
                return;
            }  
            socket_to_task_center.send(zmq::buffer(message), zmq::send_flags::dontwait);
            SPDLOG_INFO("下发至任务中心: {}", message);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("下发消息异常: {}", e.what());
        }
    });
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
