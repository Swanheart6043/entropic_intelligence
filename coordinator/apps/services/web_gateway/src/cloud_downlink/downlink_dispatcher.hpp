#pragma once
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "plan/map_handler.hpp"
#include "plan/nav_handler.hpp"
#include "plan/plan_handler.hpp"
#include "device/hardware_handler.hpp"
#include "device/version_info_handler.hpp"
#include "config/scheduled_tasks_handler.hpp"
#include "config/threshold_config_handler.hpp"
#include "config/face_database_handler.hpp"
#include "config/algorithm_config_handler.hpp"
#include "config/algorithm_area_handler.hpp"
#include "config/alarm_action_configurator.hpp"
#include "other/video_handler.hpp"
#include "video/video_extract/handler.hpp"
#include "other/media_handler.hpp"
#include "other/screenshot_handler.hpp"

class CloudDownlinkDispatcher {
public:
    explicit CloudDownlinkDispatcher(WebSocketClient& websocket_connect);
    ~CloudDownlinkDispatcher();
    void start();    
    void stop();

private:
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_task_center{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};

    WebSocketClient& websocket;
    ExtractorHandler extractor_handler;
    std::thread worker_thread;

    MapOperateHandler map_operate_handler;
    HighFreedomNavigator navigation_handler;
    PatrolPlanHandler patrol_plan_handler;

    void listen_platform();
};
