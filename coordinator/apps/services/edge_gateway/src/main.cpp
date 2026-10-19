#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "bootstrap.hpp"
#include "config_parser.hpp"
#include "state_subscriber.hpp"
#include "common/websocket_manager.hpp"
#include "components/broadcast_controller.hpp"
#include "websocket_server/edge_websocket_server.hpp"
#include "http_server/edge_http_server.hpp"
#include "mode_server/mode_server.hpp"
#include "ipc_server/ipc_pull_server.hpp"
#include "ipc_server/ipc_sub_server.hpp"
#include "forward_server/web_gateway_server.hpp"

int main() {
    try {
        patrol_robot::init_crash_handler("edge_gateway");
        patrol_robot::init_logging("edge_gateway");
        Config* config = Config::getInstance();
        config->loadFromFile("/etc/patrol_robot_h1/config.ini");        

        nlohmann::json robot_info = {
            {"sn", config->get_sn()},
            {"mode", ""},
            {"isStop", false},
            {"isCharging", false},
            {"power", 0},
            {"powerText", "0%"},
            {"temperature", 0},
            {"temperatureText", "0°C"},
            {"humidity", 0},
            {"humidityText", "0%"},
            {"pm2.5", 0},
            {"pm2.5Text", ""},
            {"totalRuntime", 0},
            {"curMilleageM", 0},
            {"curMilleageMText", "0m"},
            {"curMilleageKM", 0},
            {"curMilleageKMText", "0km"},
            {"totalMileage", 0},
            {"totalMileageText", "0km"},
            {"isHeadLightOpen", false},
            {"isWarningLightOpen", false},
            {"isWipersOpen", false},
            {"wipersMode", "single"},
            {"volume", "0"},
            {"totalPatrolPointCount", "0"},
            {"patrolledPointCount", "0"},
            {"historyPatrolPointCount", "0"},
            {"gpsInfo", ""}
        };
        std::mutex robot_info_mtx;

        EdgeWebsocketServer edge_websocket_server(robot_info, robot_info_mtx);
        EdgeHttpServer edge_http_server(robot_info, robot_info_mtx);
        EdgeModeServer edge_mode_server(robot_info, robot_info_mtx);
        EdgeIpcServer ipc_pull_server(robot_info, robot_info_mtx);
        TaskCenterServer ipc_sub_server(robot_info, robot_info_mtx);
        WebGatewayServer forward_server;

        StateSubscriber::getInstance().start();
        edge_websocket_server.start();
        edge_http_server.start();
        edge_mode_server.start();
        ipc_pull_server.start();
        ipc_sub_server.start();
        forward_server.start();

        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
        return 0;
    } catch (const std::exception& e) {
        SPDLOG_CRITICAL("边缘网关异常: {}", e.what());
        return -1;
    }
}