#include <cmath>
#include <cstddef>
#include <ctime>
#include <memory>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "map_handler.hpp"

namespace {
    using Json = nlohmann::json;

    // 导航语义标注协议不保存颜色，上传到云端时使用云端协议示例的默认色。
    constexpr const char* kDefaultRegionColor = "#1890ff";

    std::string read_map_labeling_map_name(const Json& root) {
        if (!root.is_object()) {
            throw std::invalid_argument("消息体必须是 JSON 对象");
        }

        const auto map_name_it = root.find("mapName");
        if (map_name_it == root.end() || !map_name_it->is_string()) {
            throw std::invalid_argument("mapName 必须是字符串");
        }

        const auto map_name = map_name_it->get<std::string>();
        if (map_name.empty()) {
            throw std::invalid_argument("mapName 不能为空");
        }
        return map_name;
    }

    void validate_map_labeling_point(const Json& point, const std::string& field_name, std::size_t region_index, std::size_t point_index) {
        if (!point.is_object()) {
            throw std::invalid_argument(field_name + "[" + std::to_string(region_index) + "][" + std::to_string(point_index) + "] 必须是对象");
        }

        for (const char* coordinate_name : {"x", "y"}) {
            const auto coordinate_it = point.find(coordinate_name);
            if (coordinate_it == point.end() || !coordinate_it->is_number()) {
                throw std::invalid_argument(field_name + "[" + std::to_string(region_index) + "][" + std::to_string(point_index) + "]." + coordinate_name + " 必须是数值");
            }
            if (!std::isfinite(coordinate_it->get<double>())) {
                throw std::invalid_argument(field_name + "[" + std::to_string(region_index) + "][" + std::to_string(point_index) + "]." + coordinate_name + " 必须是有限数值");
            }
        }
    }

    Json regions_to_semantic_data(const Json& regions) {
        if (!regions.is_array()) {
            throw std::invalid_argument("regions 必须是数组");
        }

        Json semantic_data = Json::array();
        for (std::size_t region_index = 0; region_index < regions.size(); ++region_index) {
            const auto& region = regions.at(region_index);
            if (!region.is_object()) {
                throw std::invalid_argument("regions[" + std::to_string(region_index) + "] 必须是对象");
            }

            const auto id_it = region.find("id");
            if (id_it == region.end() || !id_it->is_number_integer()) {
                throw std::invalid_argument("regions[" + std::to_string(region_index) + "].id 必须是整数");
            }

            const auto name_it = region.find("regionName");
            if (name_it == region.end() || !name_it->is_string() ||
                name_it->get_ref<const std::string&>().empty()) {
                throw std::invalid_argument("regions[" + std::to_string(region_index) + "].regionName 必须是非空字符串");
            }

            const auto points_it = region.find("points");
            if (points_it == region.end() || !points_it->is_array() || points_it->size() < 3) {
                throw std::invalid_argument("regions[" + std::to_string(region_index) + "].points 必须是至少包含 3 个点的数组");
            }
            for (std::size_t point_index = 0; point_index < points_it->size(); ++point_index) {
                validate_map_labeling_point(points_it->at(point_index), "regions.points", region_index, point_index);
            }

            // 云端区域协议没有单独的语义类型字段，使用 id 承载导航的 type。
            semantic_data.push_back({
                {"type", *id_it},
                {"semantic_msg", *name_it},
                {"polygon", *points_it}
            });
        }
        return semantic_data;
    }

    Json semantic_data_to_regions(const Json& semantic_data) {
        if (!semantic_data.is_array()) {
            throw std::runtime_error("导航响应 data.semantic_data 必须是数组");
        }

        Json regions = Json::array();
        for (std::size_t region_index = 0; region_index < semantic_data.size(); ++region_index) {
            const auto& semantic_item = semantic_data.at(region_index);
            if (!semantic_item.is_object()) {
                throw std::runtime_error(
                    "导航响应 data.semantic_data[" + std::to_string(region_index) + "] 必须是对象");
            }

            const auto type_it = semantic_item.find("type");
            if (type_it == semantic_item.end() || !type_it->is_number_integer()) {
                throw std::runtime_error(
                    "导航响应 data.semantic_data[" + std::to_string(region_index) +
                    "].type 必须是整数");
            }

            const auto message_it = semantic_item.find("semantic_msg");
            if (message_it == semantic_item.end() || !message_it->is_string()) {
                throw std::runtime_error(
                    "导航响应 data.semantic_data[" + std::to_string(region_index) +
                    "].semantic_msg 必须是字符串");
            }

            const auto polygon_it = semantic_item.find("polygon");
            if (polygon_it == semantic_item.end() || !polygon_it->is_array() ||
                polygon_it->size() < 3) {
                throw std::runtime_error(
                    "导航响应 data.semantic_data[" + std::to_string(region_index) +
                    "].polygon 必须是至少包含 3 个点的数组");
            }
            for (std::size_t point_index = 0; point_index < polygon_it->size(); ++point_index) {
                validate_map_labeling_point(
                    polygon_it->at(point_index), "data.semantic_data.polygon", region_index,
                    point_index);
            }

            regions.push_back({
                {"id", *type_it},
                {"regionName", *message_it},
                {"backgroundColor", kDefaultRegionColor},
                {"points", *polygon_it}
            });
        }
        return regions;
    }
}

MapOperateHandler::MapOperateHandler() {
	config->loadFromFile("/etc/patrol_robot_h1/config.ini");    
    nav_http = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
    web_http = std::make_unique<httplib::Client>(config->get_platform_http_server_url());
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    web_http->enable_server_certificate_verification(config->get_cloud_tls_verify());
    if (!config->get_cloud_ca_cert_path().empty()) {
        web_http->set_ca_cert_path(config->get_cloud_ca_cert_path());
    }
#endif
}

void MapOperateHandler::create_map(WebSocketClient& websocket_connect, const std::string& message) {

}

void MapOperateHandler::remove_map(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        auto root = nlohmann::json::parse(message);
        if (!root.contains("MapInfo") || root.at("MapInfo").empty()) {
            SPDLOG_WARN("MapInfo不能为空");
            return;
        }
        auto map_info = root.at("MapInfo");
        if (!map_info.contains("MapName") || map_info.at("MapName").get<std::string>().empty()) {
            SPDLOG_WARN("MapName不能为空");
            return;
        }
        auto map_name = map_info.at("MapName").get<std::string>();

        httplib::Result response;
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            httplib::Params params{{"map_name", map_name}};
            response = http_.Get("/gs-robot/cmd/delete_map", params, httplib::Headers{});
        }
        if (!response || response->status != 200) {
            SPDLOG_WARN("删除地图失败");
            return;
        }
        send_ack(websocket_connect, "RC_Robot_Map_Delete");
        SPDLOG_INFO("删除地图成功");
    } catch (const std::exception& e) {
        SPDLOG_WARN("删除地图异常: {}", e.what());
    }
}

void MapOperateHandler::synchronous_base_info(WebSocketClient& websocket_connect) {
    try {
        auto response = nav_http->Get("/gs-robot/data/maps");
        if (!response || response->status != 200) {
            SPDLOG_ERROR("同步地图基础信息, 查询地图失败");
            return;
        }
        auto list = nlohmann::json::parse(response->body).at("data");
        if (list.empty()) {
            SPDLOG_WARN("同步地图基础信息, 没有地图");
            return;
        }
        
        nlohmann::json answer = {
            {"Header", {
                {"CmdType", "RC_Robot_Map_Info_Get"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"MapInfoList", nlohmann::json::array()}
        };
        for (const auto& item : list) {
            nlohmann::json map_info_json = {
                {"Id", item.at("id")},
                {"MapName", item["name"]},
                {"GridWidth", item["mapInfo"]["gridWidth"]},
                {"GridHeight", item["mapInfo"]["gridHeight"]},
                {"OriginX", item["mapInfo"]["originX"]},
                {"OriginY", item["mapInfo"]["originY"]},
                {"Resolution", item["mapInfo"]["resolution"]},
                {"CreatedAt", item["createdAt"]},
            };
            answer["MapInfoList"].push_back(map_info_json);
        }
        websocket_connect.send_message(answer.dump());
        SPDLOG_INFO("同步地图基础信息完成: {}", answer.dump());

    } catch (const std::exception &e) {
        SPDLOG_ERROR("同步地图基础信息异常: {}", e.what());
    }
}

void MapOperateHandler::synchronous_map_png(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        using namespace nlohmann;

        std::string map_name = json::parse(message).at("MapInfo").at("MapName");
        if (map_name.empty()) {
            SPDLOG_ERROR("同步地图png时，地图名没传");
            return;
        }
        
        auto png_response = nav_http->Get("/gs-robot/data/map_png?map_name=" + map_name);
        if (!png_response || png_response->status != 200 || png_response->body.empty()) {
            SPDLOG_ERROR("同步地图png时，获取导航png失败");
            return;
        };
        
        httplib::MultipartFormDataItems items = {
            {"sn", config->get_sn(), "", ""},
            {"type", "3", "", ""},
            {"mapName", map_name, "", ""},
            {"file", png_response->body, "map.png", "image/png"},
        };
        auto response = web_http->Post(
            config->get_platform_http_api_path("/device/device/uploadImage"), items
        );
        if (!response || response->status != 200) {
            SPDLOG_ERROR("同步地图png时, 上传图片失败");
            return;
        };

        json answer = {
            {"Header", {
                {"CmdType", "RC_Robot_Map_File_Get"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(answer.dump());
        SPDLOG_INFO("同步地图png完成");

    } catch (const std::exception &e) {
        SPDLOG_ERROR("同步地图png异常: {}", e.what());
    }
}

void MapOperateHandler::synchronous_virtual_obstacles(const std::string& message) {
    try {
        using namespace nlohmann;

        std::string map_name = json::parse(message).at("Virtual_Obstacles");
        if (map_name.empty()) {
            SPDLOG_ERROR("同步虚拟墙时, 地图名没传");
            return;
        }
        
        auto virtual_response = nav_http->Get("/gs-robot/data/virtual_obstacles?map_name=" + map_name);
        if (!virtual_response || virtual_response->status != 200 || virtual_response->body.empty()) {
            SPDLOG_ERROR("同步虚拟墙时，获取虚拟墙失败");
            return;
        };
        
        httplib::MultipartFormDataItems items = {
            {"sn", config->get_sn(), "", ""},
            {"mapName", map_name, "", ""},
            {"vitrualWall", virtual_response->body, "", ""},
        };
        auto response = web_http->Post(
            config->get_platform_http_api_path("/device/robotInvoke/postOnVirtualWallInfo"), items
        );
        if (!response || response->status != 200) {
            SPDLOG_ERROR("同步虚拟墙时, 上传虚拟墙失败");
            return;
        };
        SPDLOG_INFO("同步虚拟墙完成: {}", virtual_response->body);
    } catch (const std::exception &e) {
        SPDLOG_ERROR("同步虚拟墙异常: {}", e.what());
    }
}

void MapOperateHandler::create_point(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        auto root = nlohmann::json::parse(message);
        if (!root.contains("Position_info") || root.at("Position_info").empty()) {
            SPDLOG_WARN("Position_info不能为空");
            return;
        }
        auto position_info = root.at("Position_info");
        if (!position_info.contains("Name") || position_info.at("Name").get<std::string>().empty()) {
            SPDLOG_WARN("Name不能为空");
            return;
        }
        if (!position_info.contains("Type") || position_info.at("Type").get<std::string>().empty()) {
            SPDLOG_WARN("Type不能为空");
            return;
        }
        auto name = position_info.at("Name").get<std::string>();
        auto type = position_info.at("Type").get<std::string>();

        httplib::Result response;
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            httplib::Params params{{"position_name", name}, {"type", type}};
            response = http_.Get("/gs-robot/cmd/add_position", params, httplib::Headers{});
        }
        if (!response || response->status != 200) {
            SPDLOG_WARN("创建点位失败");
            return;
        }
        send_ack(websocket_connect, "RC_Robot_Add_Position");
        SPDLOG_INFO("创建点位成功");
    } catch (const std::exception& e) {
        SPDLOG_WARN("创建点位时, 发生异常: {}", e.what());
    }
}

void MapOperateHandler::remove_point(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        auto root = nlohmann::json::parse(message);
        if (!root.contains("Position_info") || root.at("Position_info").empty()) {
            SPDLOG_WARN("Position_info不能为空");
            return;
        }
        auto position_info = root.at("Position_info");
        if (!position_info.contains("MapName") || position_info.at("MapName").get<std::string>().empty()) {
            SPDLOG_WARN("MapName不能为空");
            return;
        }
        if (!position_info.contains("PositionName") || position_info.at("PositionName").get<std::string>().empty()) {
            SPDLOG_WARN("PositionName不能为空");
            return;
        }
        auto map_name = position_info.at("MapName").get<std::string>();
        auto point_name = position_info.at("PositionName").get<std::string>();

        httplib::Result response;
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            httplib::Params params{{"map_name", map_name}, {"position_name", point_name}};
            response = http_.Get("/gs-robot/cmd/delete_position", params, httplib::Headers{});
        }
        if (!response || response->status != 200) {
            SPDLOG_WARN("删除点位失败");
            return;
        }
        send_ack(websocket_connect, "RC_Robot_Delete_Position");
        SPDLOG_INFO("删除点位成功");
    } catch (const std::exception& e) {
        SPDLOG_WARN("删除点位异常: {}", e.what());
    }
}

void MapOperateHandler::synchronous_point(WebSocketClient& websocket_connect, const std::string& message) {
    using namespace nlohmann;
    
    json answer = {
        {"Header", {
            { "CmdType", "RC_Robot_Get_Position_List" }, 
            { "Sn", config->get_sn() }, 
            { "Time", std::time(nullptr) }
        }},
        {"PositionList", json::array()}
    };
    auto response = nav_http->Get("/gs-robot/data/maps");
    if (!response || response->status != 200) {
        return;
    }

    auto list = json::parse(response->body).at("data");
    for (const auto& item : list) {
        auto map_name = item.at("name").get<std::string>();
        auto positions = nav_http->Get("/gs-robot/data/positions?map_name=" + map_name);
        if (!positions || positions->status != 200 || positions->body.empty()) {
            SPDLOG_ERROR("同步地图点位时，获取点位失败");
            return;
        };

        json map_item = {
            { "MapName", item["name"].get<std::string>() },
            { "Position", json::array() }
        };
        auto sub_list = json::parse(positions->body).at("data");
        for (const auto& sub_item : sub_list) {
            json position_item = {
                {"PositionName", sub_item.at("name")},
                {"Type", sub_item.at("type")},
                {"X", sub_item.at("gridX")},
                {"Y", sub_item.at("gridY")},
                {"Angle", sub_item.at("angle")}
            };
            map_item["Position"].push_back(position_item);
        }
        answer["PositionList"].push_back(map_item);
    }
    
    websocket_connect.send_message(answer.dump());
    SPDLOG_INFO("同步地图点位完成: {}", answer.dump());
}

void MapOperateHandler::read(WebSocketClient& websocket_connect) {
    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();

        // 构建SQL查询语句
        std::string sql = "SELECT * FROM preset_position";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(sql));

        // 创建一个JSON对象来存储查询结果
        using namespace nlohmann;
        json result_array = json::array();

        // 迭代结果集，将每一行转换为JSON对象
        while (res->next()) {
            json row = {
                {"PresetID", res->getInt("preset_position_code")},
                {"PresetName", res->getString("preset_position_name")},
            };
            result_array.push_back(row);
        }

        // 发送查询结果回执
        json ack = {
            {"Header", {
                { "CmdType", "RC_Robot_Preset_Get" }, 
                { "Sn", config->get_sn() }, 
                { "Time", std::time(nullptr) }
            }},
            {"Robot_Preset", {
                {"Count", result_array.size()},
                {"Presets", result_array}
            }}
        };
        websocket_connect.send_message(ack.dump());
        SPDLOG_INFO("预置位同步完成: {}", ack.dump());
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("查询预置位, 数据库错误: {}", e.what());
    } catch (std::exception &e) {
        SPDLOG_ERROR("查询预置位, 发生异常: {}", e.what());
    }
}

void MapOperateHandler::remove(WebSocketClient& websocket_connect, const std::string& message) {
    sql::Connection* con = nullptr;
    try {
        using namespace nlohmann;

        auto code = json::parse(message).at("Robot_Preset").at("Preset").at("PresetID").get<std::string>();

        con = DBConnection::get_instance().get_connection();
        con->setAutoCommit(false);

        // 构建SQL
        std::string sql = "DELETE FROM preset_position WHERE preset_position_code = ?";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        pstmt->setString(1, code);

        // 执行删除
        pstmt->executeUpdate();

        // 提交事务
        con->commit();
        con->setAutoCommit(true);

        SPDLOG_INFO("删除预置位成功");

        // 发送回执
        json ack = {
            {"Header", {
                {"CmdType", "RC_Robot_Preset_Delete"}, 
                {"Sn", config->get_sn()}, 
                {"Time", std::time(nullptr)}
            }}, 
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("删除预置位, 数据库错误: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("删除预置位, 发生异常: {}", e.what());
        if (con) {
            try { con->rollback(); con->setAutoCommit(true); } catch (...) {}
        }
    }
}

// 下载地图标注数据
void MapOperateHandler::download_map_labeling(WebSocketClient& websocket_connect, const std::string& message) {
    auto send_result = [&](int result, const std::string& data) {
        const Json ack = {
            {"Header", {
                {"CmdType", "RC_MapRegion_Set"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", result},
            {"Data", data}
        };
        if (!websocket_connect.send_message(ack.dump())) {
            SPDLOG_ERROR("地图标注下发响应发送失败: Result={}", result);
        }
    };

    try {
        const Json root = Json::parse(message);
        const std::string map_name = read_map_labeling_map_name(root);

        const auto regions_it = root.find("regions");
        if (regions_it == root.end()) {
            throw std::invalid_argument("缺少 regions");
        }

        const Json request_body = {
            {"data", {
                {"map_name", map_name},
                {"semantic_data", regions_to_semantic_data(*regions_it)}
            }},
            {"errorCode", ""},
            {"msg", "success"},
            {"successed", true}
        };

        httplib::Result response;
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            response = nav_http->Post("/gs-robot/data/set_semantic_data", request_body.dump(), "application/json");
        }
        if (!response) {
            throw std::runtime_error("请求导航设置语义地图标注失败");
        }
        if (response->status != 200) {
            throw std::runtime_error("导航设置语义地图标注返回 HTTP " + std::to_string(response->status));
        }
        const Json response_body = Json::parse(response->body);
        const auto success_it = response_body.find("successed");
        if (!response_body.is_object() || success_it == response_body.end() ||
            !success_it->is_boolean() || !success_it->get<bool>()) {
            throw std::runtime_error("导航设置语义地图标注返回失败: " + response->body);
        }
        send_result(1, "");
        SPDLOG_INFO("地图 {} 的标注数据存入导航成功，共 {} 个区域", map_name, request_body.at("data").at("semantic_data").size());
    } catch (const std::exception& e) {
        SPDLOG_WARN("地图标注下发失败: {}", e.what());
        send_result(0, e.what());
    }
}

// 上传地图标注数据
void MapOperateHandler::upload_map_labeling(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        const Json root = Json::parse(message);
        const std::string map_name = read_map_labeling_map_name(root);

        httplib::Result response;
        {
            std::lock_guard<std::mutex> lock(http_mutex_);
            const httplib::Params params{{"map_name", map_name}};
            response = nav_http->Get("/gs-robot/data/get_semantic_data", params, httplib::Headers{});
        }
        if (!response) {
            throw std::runtime_error("请求导航获取语义地图标注失败");
        }
        if (response->status != 200) {
            throw std::runtime_error("导航获取语义地图标注返回 HTTP " + std::to_string(response->status));
        }

        const Json response_body = Json::parse(response->body);
        if (!response_body.is_object()) {
            throw std::runtime_error("导航获取语义地图标注响应必须是 JSON 对象");
        }
        const auto success_it = response_body.find("successed");
        if (success_it == response_body.end() || !success_it->is_boolean() ||
            !success_it->get<bool>()) {
            throw std::runtime_error("导航获取语义地图标注返回失败: " + response->body);
        }

        const auto data_it = response_body.find("data");
        if (data_it == response_body.end() || !data_it->is_object()) {
            throw std::runtime_error("导航响应缺少 data 对象");
        }
        const auto response_map_name_it = data_it->find("map_name");
        if (response_map_name_it == data_it->end() || !response_map_name_it->is_string()) {
            throw std::runtime_error("导航响应缺少字符串类型的 data.map_name");
        }
        if (response_map_name_it->get<std::string>() != map_name) {
            throw std::runtime_error("导航响应中的 data.map_name 与请求不一致");
        }
        const auto semantic_data_it = data_it->find("semantic_data");
        if (semantic_data_it == data_it->end()) {
            throw std::runtime_error("导航响应缺少 data.semantic_data");
        }

        Json answer = {
            {"Header", {
                {"CmdType", "RC_MapRegion_Get"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"mapName", map_name},
            {"regions", semantic_data_to_regions(*semantic_data_it)}
        };

        if (!websocket_connect.send_message(answer.dump())) {
            SPDLOG_ERROR("地图 {} 的标注数据查询成功，但响应发送失败", map_name);
            return;
        }
        SPDLOG_INFO("地图 {} 的标注数据同步成功，共 {} 个区域", map_name, answer.at("regions").size());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("地图标注同步失败: {}", e.what());
    }
}

void MapOperateHandler::send_ack(WebSocketClient& websocket_connect, const std::string& cmd_type) {
    nlohmann::json ack = {
        {"Header", {
            {"CmdType", cmd_type},
            {"Sn", config_->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1},
        {"Data", ""}
    };
    websocket_connect.send_message(ack.dump());
}
