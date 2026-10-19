#include <iostream>
#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "downlink_dispatcher.hpp"

CloudDownlinkDispatcher::CloudDownlinkDispatcher(WebSocketClient& websocket_connect)
    : websocket(websocket_connect),
      extractor_handler(websocket_connect),
      navigation_handler(websocket_connect) {
    socket_to_task_center.connect("tcp://127.0.0.1:4004");
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
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
}

void CloudDownlinkDispatcher::listen_platform() {
    websocket.set_message_callback([&](const std::string& message) {
        SPDLOG_INFO("收到Web云层命令: {}", message);

        std::string command = nlohmann::json::parse(message).at("Header").at("CmdType");
        if (command == "HeartbeatConfirm") return;
        if (command == "CommandConfirm") return;
        
        try {
            // 同步地图信息 - 低频
            if (command == "RC_Robot_Map_Info_Get") {
                map_operate_handler.synchronous_base_info(websocket);
                return;
            }
            // 同步地图PNG - 低频
            if (command == "RC_Robot_Map_File_Get") {
                map_operate_handler.synchronous_map_png(websocket, message);
                return;
            }
            // 同步虚拟墙(走http) - 低频
            if (command == "RC_Robot_Virtual_Obstacles_Get") {
                map_operate_handler.synchronous_virtual_obstacles(message);
                return;
            }
            // 同步点位 - 低频
            if (command == "RC_Robot_Get_Position_List") {
                map_operate_handler.synchronous_point(websocket, message);
                return;
            }
            // 删除地图 - 低频
            if (command == "RC_Robot_Map_Delete") {
                map_operate_handler.remove_map(websocket, message);
                return;
            }
            // 创建点位 - 低频
            if (command == "RC_Robot_Add_Position") {
                map_operate_handler.create_point(websocket, message);
                return;
            }
            // 删除点位 - 低频
            if (command == "RC_Robot_Delete_Position") {
                map_operate_handler.remove_point(websocket, message);
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
            // 保存巡逻计划 - 低频
            if (command == "NB_RobotPatrolPlan_Set") {
                patrol_plan_handler.save_patrol_plan(websocket, message);
                return;
            }
            // 保存值守计划 - 低频
            if (command == "NB_RobotTaskPlan_Set") {
                patrol_plan_handler.save_duty_plan(websocket, message);
                return;
            }
            // 高自由度导航 - 高频
            if (command == "RC_Robot_Gs_Data_Get") {
                navigation_handler.submit(message);
                return;
            }
            // 硬件信息 - 低频
            if (command == "RC_Robot_Base_Data_Set") {
                HardwareInfoHandler hardware_info_handler;
                hardware_info_handler.handle_hardware_info(websocket);
                return;
            }
            // 使能信息 - 低频
            if (command == "RC_RobotEnable_Get") {
                HardwareInfoHandler hardware_info_handler;
                hardware_info_handler.handle_switch_info(websocket, message);
                return;
            }
            // 版本信息 - 低频
            if (command == "RC_RobotVersion_Get") {
                VersionInfoHandler version_info_handler;
                version_info_handler.handle(websocket, message);
                return;
            }
            // 定时任务 - 低频
            if (command == "RC_RobotEnableTask_Set") {
                ScheduledTasksHandler scheduled_tasks_handler;
                scheduled_tasks_handler.handle(websocket, message);
                return;
            }
            // 阈值配置 - 低频
            if (command == "RC_RobotEnvConfig_Set") {
                ThresholdConfigHandler threshold_config_handler;
                threshold_config_handler.handle(websocket, message);
                return;
            }
            // 人员数据下发 - 低频
            if (command == "RC_Robot_Register_Face_Update_Notice") {
                FaceDatabaseHandler face_database_handler;
                face_database_handler.handle(websocket, message);
                return;
            }
            // 告警配置 - 低频
            if (command == "RC_Robot_DEAL_ALRAM") {
                AlarmConfigHandler alarm_config_handler;
                alarm_config_handler.handle(websocket, message);
                return;
            }
            // 算法区域下发 - 低频
            if (command == "RC_AlgorithmArea_Set") {
                AlgorithmAreasHandler algorithm_area_handler;
                algorithm_area_handler.download_algorithm_areas(websocket, message);
                return;
            }
            // 算法区域同步 - 低频
            if (command == "RC_AlgorithmArea_Get") {
                AlgorithmAreasHandler algorithm_area_handler;
                algorithm_area_handler.upload_algorithm_areas(websocket, message);
                return;
            }
            // 算法配置下发 - 低频
            if (command == "RC_FuncConfig_Set") {
                AlgorithmConfigHandler algorithm_config_handler;
                algorithm_config_handler.download_algorithm_config(websocket, message);
                return;
            }
            // 算法配置同步 - 低频
            if (command == "RC_FuncConfig_Get") {
                AlgorithmConfigHandler algorithm_config_handler;
                algorithm_config_handler.upload_algorithm_config(websocket, message);
                return;
            }
            // 地图标注下发 - 低频
            if (command == "RC_MapRegion_Set") {
                map_operate_handler.download_map_labeling(websocket, message);
                return;
            }
            // 地图标注同步 - 低频
            if (command == "RC_MapRegion_Get") {
                map_operate_handler.upload_map_labeling(websocket, message);
                return;
            }
            // 人脸配置 - 低频
            if (command == "RC_FaceRecognized_Broadcast_Set") {
                FaceDatabaseHandler face_database_handler;
                face_database_handler.answer(websocket, message);
                return;
            }
            // 播报文字 - 低频
            if (command == "VI_ROBOT_SOUNDTEXT_SET") {
                MediaHandler media_handler;
                media_handler.handle_broadcast_text(websocket, message);
                return;
            }
            // 控制开关 - 低频
            if (command == "RC_RobotEnable_Set") {
                MediaHandler media_handler;
                media_handler.handle_switch(websocket, message);
                return;
            }
            // 监听对讲 - 低频
            if (command == "VoiceInteractionType_Set") {
                MediaHandler media_handler;
                media_handler.handle_listening_and_talking(websocket, message);
                return;
            }
            // 显示屏标语 - 低频
            if (command == "TS_Slogan_Config_Info") {
                MediaHandler media_handler;
                media_handler.handle_screen_display(websocket, message);
                return;
            }
            // SOS挂断 - 低频
            if (command == "Call_SOS_hangUp") {
                MediaHandler media_handler;
                media_handler.handle_sos_end(websocket, message);
                return;
            }
            // 人工触发截图 - 中频
            if (command == "RC_Robot_Alarm_Screenshot") {
                ScreenshotHandler screenshot_handler;
                screenshot_handler.handle_message(websocket, message);
                return;
            }
            // 视频 - 录像文件截取
            if (command == "RC_Robot_Offline_RecordVideo_Get") {
                extractor_handler.submit(message);
                return;
            }
                        
            // 视频 - 推流
            if (command == "PTZ_Video_Url_Get") {
                AudioController video_controller;
                video_controller.notification_streaming(websocket, message);
                return;
            }
            // 开关骨骼推流
            if (command == "PTZ_AlgorithmVideo_Url_Get") {
                AudioController video_controller;
                video_controller.handle_skeleton_streaming(websocket, message);
                return;
            }
            // 视频 - 查询录像列表
            if (command == "RC_Robot_Offline_Videolist_Get") {
                AudioController video_controller;
                video_controller.handle_video_file_list(websocket, message);
                return;
            }
            // 视频 - 录像文件推流
            if (command == "PTZ_RecordVideo_Url_Get") {
                AudioController video_controller;
                video_controller.handle_video_file_streaming(websocket, message);
                return;
            }
            // 视频 - 录像推流带参
            if (command == "PTZ_RecordVideo_Control") {
                AudioController video_controller;
                video_controller.handle_video_file_control(websocket, message);
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
