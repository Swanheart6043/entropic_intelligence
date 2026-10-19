#pragma once
#include <optional>
#include <string>
#include "httplib.hpp"

namespace charging {

struct ChargingPoint {
    std::string map_name;
    std::string point_name;
};

// 纯查询：查当前地图的第一个充电点(type=1)，任何一步失败返回 nullopt
std::optional<ChargingPoint> find_charging_point(httplib::Client& http);

// 便捷封装：查 + 导航一次，成功返回 true
bool navigate_to_charging_point(httplib::Client& http);

}