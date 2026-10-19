#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "algorithm_area_handler.hpp"
#include "db_connection.hpp"

namespace {
    using Json = nlohmann::json;
    constexpr std::size_t kMaxMapNameCharacters = 128;

    std::size_t utf8_character_count(const std::string& value) {
        std::size_t count = 0;
        for (const unsigned char byte : value) {
            if ((byte & 0xC0U) != 0x80U) {
                ++count;
            }
        }
        return count;
    }

    std::string read_map_name(const Json& message_json) {
        if (!message_json.is_object()) {
            throw std::invalid_argument("消息体必须是 JSON 对象");
        }
        const auto map_name_it = message_json.find("mapName");
        if (map_name_it == message_json.end() || !map_name_it->is_string()) {
            throw std::invalid_argument("mapName 必须是字符串");
        }
        const std::string map_name = map_name_it->get<std::string>();
        if (map_name.empty()) {
            throw std::invalid_argument("mapName 不能为空");
        }
        if (utf8_character_count(map_name) > kMaxMapNameCharacters) {
            throw std::invalid_argument("mapName 长度不能超过 128 个字符");
        }
        return map_name;
    }

    void validate_algorithm_type(const Json& value, std::size_t area_index) {
        bool valid = false;
        if (value.is_number_unsigned()) {
            valid = value.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<int>::max());
        } else if (value.is_number_integer()) {
            const auto algorithm_type = value.get<std::int64_t>();
            valid = algorithm_type >= 0 && algorithm_type <= static_cast<std::int64_t>(std::numeric_limits<int>::max());
        }
        if (!valid) {
            throw std::invalid_argument("algorithmAreas[" + std::to_string(area_index) + "].algorithmType 必须是非负整数");
        }
    }

    void validate_coordinate(const Json& point, const char* coordinate_name, std::size_t area_index, std::size_t point_index) {
        const auto coordinate_it = point.find(coordinate_name);
        if (coordinate_it == point.end() || !coordinate_it->is_number()) {
            throw std::invalid_argument("algorithmAreas[" + std::to_string(area_index) + "].points[" + std::to_string(point_index) + "]." + coordinate_name + " 必须是数值");
        }

        const double coordinate = coordinate_it->get<double>();
        if (!std::isfinite(coordinate)) {
            throw std::invalid_argument("algorithmAreas[" + std::to_string(area_index) + "].points[" + std::to_string(point_index) + "]." + coordinate_name + " 必须是有限数值");
        }
    }

    void validate_algorithm_areas(const Json& algorithm_areas) {
        if (!algorithm_areas.is_array()) {
            throw std::invalid_argument("algorithmAreas 必须是数组");
        }

        for (std::size_t area_index = 0; area_index < algorithm_areas.size(); ++area_index) {
            const auto& area = algorithm_areas.at(area_index);
            if (!area.is_object()) {
                throw std::invalid_argument(
                    "algorithmAreas[" + std::to_string(area_index) + "] 必须是对象");
            }

            const auto algorithm_name_it = area.find("algorithmName");
            if (algorithm_name_it == area.end() ||
                !algorithm_name_it->is_string() ||
                algorithm_name_it->get_ref<const std::string&>().empty()) {
                throw std::invalid_argument(
                    "algorithmAreas[" + std::to_string(area_index) +
                    "].algorithmName 必须是非空字符串");
            }

            const auto algorithm_type_it = area.find("algorithmType");
            if (algorithm_type_it == area.end()) {
                throw std::invalid_argument(
                    "algorithmAreas[" + std::to_string(area_index) +
                    "] 缺少 algorithmType");
            }
            validate_algorithm_type(*algorithm_type_it, area_index);

            const auto points_it = area.find("points");
            if (points_it == area.end() || !points_it->is_array() || points_it->size() < 3) {
                throw std::invalid_argument(
                    "algorithmAreas[" + std::to_string(area_index) +
                    "].points 必须是至少包含 3 个点的数组");
            }

            for (std::size_t point_index = 0; point_index < points_it->size(); ++point_index) {
                const auto& point = points_it->at(point_index);
                if (!point.is_object()) {
                    throw std::invalid_argument(
                        "algorithmAreas[" + std::to_string(area_index) + "].points[" +
                        std::to_string(point_index) + "] 必须是对象");
                }
                validate_coordinate(point, "x", area_index, point_index);
                validate_coordinate(point, "y", area_index, point_index);
            }
        }
    }
}

AlgorithmAreasHandler::AlgorithmAreasHandler() = default;

AlgorithmAreasHandler::~AlgorithmAreasHandler() = default;

void AlgorithmAreasHandler::upload_algorithm_areas(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        const Json message_json = Json::parse(message);
        const std::string map_name = read_map_name(message_json);

        auto* connection = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement("SELECT area_data FROM algorithm_areas WHERE map_name = ?"));
        statement->setString(1, map_name);
        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        Json algorithm_areas = Json::array();
        if (result->next()) {
            const std::string area_data = result->getString("area_data");
            if (!area_data.empty()) {
                algorithm_areas = Json::parse(area_data);
                validate_algorithm_areas(algorithm_areas);
            }
        }

        const Json response = {
            {"Header", {
                {"CmdType", "RC_AlgorithmArea_Get"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"mapName", map_name},
            {"algorithmAreas", std::move(algorithm_areas)}
        };

        if (!websocket_connect.send_message(response.dump())) {
            SPDLOG_ERROR("地图 {} 的算法区域查询成功，但响应发送失败", map_name);
            return;
        }
        SPDLOG_INFO("地图 {} 的算法区域同步成功，共 {} 个区域", map_name, response.at("algorithmAreas").size());
    } catch (const sql::SQLException& e) {
        SPDLOG_ERROR("查询算法区域时发生数据库错误: {}", e.what());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("查询算法区域失败: {}", e.what());
    }
}

void AlgorithmAreasHandler::download_algorithm_areas(WebSocketClient& websocket_connect, const std::string& message) {
    auto send_ack = [&](int result, const std::string& data) {
        try {
            const Json ack = {
                {"Header", {
                    {"CmdType", "RC_AlgorithmArea_Set"},
                    {"Sn", config->get_sn()},
                    {"Time", std::time(nullptr)}
                }},
                {"Result", result},
                {"Data", data}
            };
            if (!websocket_connect.send_message(ack.dump())) {
                SPDLOG_ERROR("算法区域下发响应发送失败: Result={}", result);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("组装或发送算法区域下发响应失败: {}", e.what());
        }
    };
    try {
        const Json message_json = Json::parse(message);
        const std::string map_name = read_map_name(message_json);

        const auto algorithm_areas_it = message_json.find("algorithmAreas");
        if (algorithm_areas_it == message_json.end()) {
            throw std::invalid_argument("缺少 algorithmAreas");
        }
        validate_algorithm_areas(*algorithm_areas_it);

        auto* connection = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement("INSERT INTO algorithm_areas (map_name, area_data) VALUES (?, ?) ON DUPLICATE KEY UPDATE area_data = VALUES(area_data)"));
        statement->setString(1, map_name);
        statement->setString(2, algorithm_areas_it->dump());
        statement->executeUpdate();

        send_ack(1, "");
        SPDLOG_INFO("地图 {} 的算法区域保存成功，共 {} 个区域", map_name, algorithm_areas_it->size());
    } catch (const sql::SQLException& e) {
        SPDLOG_ERROR("保存算法区域时发生数据库错误: {}", e.what());
        send_ack(0, "database error");
    } catch (const std::exception& e) {
        SPDLOG_WARN("保存算法区域失败: {}", e.what());
        send_ack(0, e.what());
    }
}