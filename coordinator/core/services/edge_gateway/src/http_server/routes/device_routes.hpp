#pragma once
#include <mutex>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "../common/i18n.hpp"

void register_device_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx);
void register_version_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx);
void register_detection_routes(httplib::Server& server, nlohmann::json& robot_info, std::mutex& robot_info_mtx);