#include <spdlog/spdlog.h>
#include "components/broadcast_controller.hpp"
#include "../common/http_helpers.hpp"
#include "../controller/device_controller.hpp"
#include "device_routes.hpp"

using namespace http_helpers;

void register_device_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx) {
    auto dev = std::make_shared<DeviceInfoController>();

    server.Get("/query-device-info", safe([&robot_info, &robot_info_mtx](const httplib::Request& req) {
        nlohmann::json snapshot;
        {
            std::lock_guard<std::mutex> lk(robot_info_mtx);
            snapshot = robot_info;
        }
        return ok_body(std::move(snapshot), i18n::Msg::QuerySuccess);
    }));
    server.Post("/change-mode", safe([dev](const httplib::Request& req) {
        return dev->change_mode(req);
    }));
    server.Post("/charging", safe([dev](const httplib::Request& req) {
        return dev->charging(req);
    }));
    server.Post("/control-light", safe([dev](const httplib::Request& req) {
        return dev->control_light(req);
    }));
    server.Post("/control-ptz", safe([dev](const httplib::Request& req) {
        return dev->control_ptz(req);
    }));
    server.Post("/control-wiper", safe([dev](const httplib::Request& req) {
        return dev->control_wiper(req);
    }));
    server.Post("/control-volume", safe([dev](const httplib::Request& req) {
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string volume = body.value("volume", "");
        BroadcastController b;
        b.control_volume(volume);
        return ok_body();
    }));
    server.Post("/broadcast-text", safe([dev](const httplib::Request& req) {
        nlohmann::json body = nlohmann::json::parse(req.body);
        std::string content = body.value("content", "");
        std::string play_mode = body.value("playMode", "");
        std::string duration = body.value("duration", "");
        BroadcastController b;
        b.broadcast_text(content, play_mode, duration);
        return ok_body();
    }));
    // 版本相关
    server.Get("/edge-hardware-version", safe([dev](const httplib::Request& req) {
        return dev->query_edge_hardware_version(req);
    }));
    server.Post("/edge-hardware-version", safe([dev](const httplib::Request& req) {
        return dev->save_edge_hardware_version(req);
    }));
    // 目标检测相关
    server.Get("/algorithm-type", safe([dev](const httplib::Request& req) {
        return dev->query_algorithm_type(req);
    }));
    server.Get("/algorithm-area", safe([dev](const httplib::Request& req) {
        return dev->query_algorithm_area(req);
    }));
    server.Post("/sync-algorithm-area", safe([dev](const httplib::Request& req) {
        return dev->save_algorithm_area(req);
    }));
}

// 版本相关
void register_version_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx) {

}

// 目标检测相关
void register_detection_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx) {

}