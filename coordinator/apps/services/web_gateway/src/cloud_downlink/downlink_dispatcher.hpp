#pragma once
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"
#include "plan/map_handler.hpp"
#include "plan/plan_handler.hpp"
#include "config/face_database_handler.hpp"

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
    std::thread worker_thread;
    
    MapOperateHandler map_operate_handler;
    PatrolPlanHandler patrol_plan_handler;
    void listen_platform();
};
