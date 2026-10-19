#pragma once
#include <mutex>
#include <nlohmann/json.hpp>
#include "httplib.hpp"

void register_user_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx);