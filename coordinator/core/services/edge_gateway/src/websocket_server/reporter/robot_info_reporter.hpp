#pragma once
#include <mutex>
#include <nlohmann/json.hpp>

void report_robot_info(nlohmann::json& robot_info, std::mutex& robot_info_mtx);