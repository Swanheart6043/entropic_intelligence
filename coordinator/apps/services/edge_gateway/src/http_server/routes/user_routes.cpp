#include "../common/http_helpers.hpp"
#include "../controller/user_controller.hpp"
#include "user_routes.hpp"

using namespace http_helpers;

void register_user_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx) {
    server.Post("/login", safe([](const httplib::Request& req) {
        UserController user_controller;
        return user_controller.login(req);
    }));
}