#include "charging_point.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "component/navigate_executor/navigator.hpp"

namespace charging {
    std::optional<ChargingPoint> find_charging_point(httplib::Client& http) {
        using namespace nlohmann;

        auto init = http.Get("/gs-robot/real_time_data/current_initialize_status");
        if (!init || init->status != 200) {
            SPDLOG_WARN("获取初始化信息失败!");
            return std::nullopt;
        }
        auto map_name = json::parse(init->body).value("currentMap", "");
        if (map_name.empty()) {
            SPDLOG_WARN("获取地图失败!");
            return std::nullopt;
        }

        auto resp = http.Get("/gs-robot/data/positions?map_name=" + map_name + "&type=1");
        if (!resp || resp->status != 200) {
            SPDLOG_WARN("获取充电点失败!");
            return std::nullopt;
        }
        auto data = json::parse(resp->body).at("data");
        if (data.empty()) {
            SPDLOG_WARN("当前地图没有充电点!");
            return std::nullopt;
        }
        return ChargingPoint{map_name, data[0].at("name").get<std::string>()};
    }

    bool navigate_to_charging_point(httplib::Client& http) {
        auto cp = find_charging_point(http);
        if (!cp) return false;
        return Navigator::instance().nav_to_normal_point(cp->map_name, cp->point_name) == NavigationResult::SUCCESS;
    }
}