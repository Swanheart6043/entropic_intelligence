#pragma once
#include <string>
#include <iostream>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "../common/i18n.hpp"

class DeviceInfoController {
public:
    DeviceInfoController();
    ~DeviceInfoController();

    std::string change_mode(const httplib::Request&);
    std::string charging(const httplib::Request&);
    std::string control_ptz(const httplib::Request&);
    std::string control_wiper(const httplib::Request&);
    std::string control_light(const httplib::Request&);
    nlohmann::json save_edge_hardware_version(const httplib::Request&);
    std::string query_edge_hardware_version(const httplib::Request&);
    std::string query_algorithm_type(const httplib::Request&);
    nlohmann::json save_algorithm_area(const httplib::Request&);
    std::string query_algorithm_area(const httplib::Request&);
    nlohmann::json remove_algorithm_area(const httplib::Request&);
    
private:
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_task_center{ctx, zmq::socket_type::push};

    static std::string success(i18n::Msg m = i18n::Msg::OperationSuccess) {
        return nlohmann::json{
            {"success", true},
            {"message", i18n::text(m)},
            {"code", 200},
            {"result", nullptr}
        }.dump();
    }
    static std::string fail(int code, i18n::Msg m) {
        return nlohmann::json{
            {"success", false},
            {"message", i18n::text(m)},
            {"code", code},
            {"result", nullptr}
        }.dump();
    }
};