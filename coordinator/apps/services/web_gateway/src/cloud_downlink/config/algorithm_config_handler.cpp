#include <cstddef>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "algorithm_config_handler.hpp"
#include "db_connection.hpp"

namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaxAlgorithmNameCharacters = 100;

struct AlgorithmConfig {
    int type = 0;
    std::string name;
    int channel = 0;
    int by_region = 0;
    int enabled = 0;
    std::string start_date;
    std::string end_date;
    Json time_ranges = Json::array();
};

std::string field_path(std::size_t index, const char* field_name) {
    return "Platanus[" + std::to_string(index) + "]." + field_name;
}

std::size_t utf8_character_count(const std::string& value) {
    std::size_t count = 0;
    for (const unsigned char byte : value) {
        if ((byte & 0xC0U) != 0x80U) {
            ++count;
        }
    }
    return count;
}

const Json& required_field(
    const Json& item,
    const char* field_name,
    std::size_t index) {
    const auto field = item.find(field_name);
    if (field == item.end() || field->is_null()) {
        throw std::invalid_argument(field_path(index, field_name) + " 不能为空");
    }
    return *field;
}

std::string required_string(
    const Json& item,
    const char* field_name,
    std::size_t index) {
    const auto& value = required_field(item, field_name, index);
    if (!value.is_string()) {
        throw std::invalid_argument(field_path(index, field_name) + " 必须是字符串");
    }

    const std::string result = value.get<std::string>();
    if (result.empty()) {
        throw std::invalid_argument(field_path(index, field_name) + " 不能为空");
    }
    return result;
}

int decimal_string_to_int(const std::string& value, const std::string& path) {
    if (value.empty()) {
        throw std::invalid_argument(path + " 不能为空");
    }

    int result = 0;
    for (const unsigned char character : value) {
        if (character < '0' || character > '9') {
            throw std::invalid_argument(path + " 必须是非负整数");
        }
        const int digit = character - '0';
        if (result > (std::numeric_limits<int>::max() - digit) / 10) {
            throw std::invalid_argument(path + " 超出 int 范围");
        }
        result = result * 10 + digit;
    }
    return result;
}

int non_negative_int(
    const Json& item,
    const char* field_name,
    std::size_t index) {
    const auto& value = required_field(item, field_name, index);
    const std::string path = field_path(index, field_name);

    if (value.is_number_unsigned()) {
        const auto result = value.get<std::uint64_t>();
        if (result <= static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            return static_cast<int>(result);
        }
    } else if (value.is_number_integer()) {
        const auto result = value.get<std::int64_t>();
        if (result >= 0 &&
            result <= static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
            return static_cast<int>(result);
        }
    } else if (value.is_string()) {
        return decimal_string_to_int(value.get<std::string>(), path);
    }

    throw std::invalid_argument(path + " 必须是非负整数");
}

int zero_or_one(
    const Json& item,
    const char* field_name,
    std::size_t index,
    bool allow_empty_string = false) {
    const auto& value = required_field(item, field_name, index);
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    if (allow_empty_string && value.is_string() && value.get_ref<const std::string&>().empty()) {
        return 0;
    }

    const int result = non_negative_int(item, field_name, index);
    if (result != 0 && result != 1) {
        throw std::invalid_argument(field_path(index, field_name) + " 只能是 0 或 1");
    }
    return result;
}

int fixed_digits(const std::string& value, std::size_t offset, std::size_t count) {
    int result = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const char character = value[offset + i];
        if (character < '0' || character > '9') {
            return -1;
        }
        result = result * 10 + (character - '0');
    }
    return result;
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

void validate_date(const std::string& value, const std::string& path) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        throw std::invalid_argument(path + " 格式必须是 YYYY-MM-DD");
    }

    const int year = fixed_digits(value, 0, 4);
    const int month = fixed_digits(value, 5, 2);
    const int day = fixed_digits(value, 8, 2);
    if (year <= 0 || month <= 0 || month > 12 || day <= 0) {
        throw std::invalid_argument(path + " 不是有效日期");
    }

    const int days_in_month[] = {
        31, is_leap_year(year) ? 29 : 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (day > days_in_month[month - 1]) {
        throw std::invalid_argument(path + " 不是有效日期");
    }
}

int time_to_seconds(const std::string& value, const std::string& path) {
    if (value.size() != 8 || value[2] != ':' || value[5] != ':') {
        throw std::invalid_argument(path + " 格式必须是 HH:MM:SS");
    }

    const int hour = fixed_digits(value, 0, 2);
    const int minute = fixed_digits(value, 3, 2);
    const int second = fixed_digits(value, 6, 2);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 59) {
        throw std::invalid_argument(path + " 不是有效时间");
    }
    return hour * 3600 + minute * 60 + second;
}

Json normalize_time_ranges(const Json& item, std::size_t index) {
    const auto& raw_time = required_field(item, "Time", index);
    Json time_envelope;
    if (raw_time.is_string()) {
        const std::string text = raw_time.get<std::string>();
        if (text.empty()) {
            time_envelope = Json{{"time", Json::array()}};
        } else {
            try {
                time_envelope = Json::parse(text);
            } catch (const Json::exception& e) {
                throw std::invalid_argument(
                    field_path(index, "Time") + " 不是有效 JSON: " + e.what());
            }
        }
    } else if (raw_time.is_object()) {
        // 兼容调用方直接传 JSON 对象，上传云端时仍统一编码为字符串。
        time_envelope = raw_time;
    } else {
        throw std::invalid_argument(field_path(index, "Time") + " 必须是 JSON 字符串或对象");
    }

    if (!time_envelope.is_object() ||
        !time_envelope.contains("time") ||
        !time_envelope.at("time").is_array()) {
        throw std::invalid_argument(
            field_path(index, "Time") + " 必须包含 time 数组");
    }

    const auto& periods = time_envelope.at("time");
    for (std::size_t period_index = 0; period_index < periods.size(); ++period_index) {
        const auto& period = periods.at(period_index);
        const std::string period_path = field_path(index, "Time") +
            ".time[" + std::to_string(period_index) + "]";
        if (!period.is_object() ||
            !period.contains("start_time") ||
            !period.at("start_time").is_string() ||
            !period.contains("end_time") ||
            !period.at("end_time").is_string()) {
            throw std::invalid_argument(
                period_path + " 必须包含字符串 start_time 和 end_time");
        }

        const std::string start_time = period.at("start_time").get<std::string>();
        const std::string end_time = period.at("end_time").get<std::string>();
        const int start_seconds = time_to_seconds(start_time, period_path + ".start_time");
        const int end_seconds = time_to_seconds(end_time, period_path + ".end_time");
        if (start_seconds >= end_seconds) {
            throw std::invalid_argument(period_path + " 的 start_time 必须早于 end_time");
        }
    }

    return periods;
}

AlgorithmConfig parse_algorithm_config(const Json& item, std::size_t index) {
    if (!item.is_object()) {
        throw std::invalid_argument(
            "Platanus[" + std::to_string(index) + "] 必须是对象");
    }

    AlgorithmConfig config;
    config.type = non_negative_int(item, "Type", index);
    config.name = required_string(item, "Name", index);
    if (utf8_character_count(config.name) > kMaxAlgorithmNameCharacters) {
        throw std::invalid_argument(
            field_path(index, "Name") + " 长度不能超过 100 个字符");
    }
    config.channel = non_negative_int(item, "Channel", index);
    config.by_region = zero_or_one(item, "ByRegion", index, true);
    config.enabled = zero_or_one(item, "Enable", index);
    config.start_date = required_string(item, "StartDate", index);
    config.end_date = required_string(item, "EndDate", index);
    validate_date(config.start_date, field_path(index, "StartDate"));
    validate_date(config.end_date, field_path(index, "EndDate"));
    if (config.start_date > config.end_date) {
        throw std::invalid_argument(
            field_path(index, "StartDate") + " 不能晚于 EndDate");
    }
    config.time_ranges = normalize_time_ranges(item, index);
    return config;
}

Json to_json(const AlgorithmConfig& config) {
    return {
        {"Type", config.type},
        {"Name", config.name},
        {"Channel", config.channel},
        {"ByRegion", config.by_region},
        {"Enable", config.enabled},
        {"StartDate", config.start_date},
        {"EndDate", config.end_date},
        {"Time", Json{{"time", config.time_ranges}}.dump()}
    };
}

}  // namespace

AlgorithmConfigHandler::AlgorithmConfigHandler() = default;

AlgorithmConfigHandler::~AlgorithmConfigHandler() = default;

void AlgorithmConfigHandler::download_algorithm_config(
    WebSocketClient& websocket_connect,
    const std::string& message) {
    auto send_ack = [&](int result, const std::string& data) {
        try {
            const Json ack = {
                {"Header", {
                    {"CmdType", "RC_FuncConfig_Set"},
                    {"Sn", config_->get_sn()},
                    {"Time", std::time(nullptr)}
                }},
                {"Result", result},
                {"Data", data}
            };
            if (!websocket_connect.send_message(ack.dump())) {
                SPDLOG_ERROR("算法配置下发响应发送失败: Result={}", result);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("组装或发送算法配置下发响应失败: {}", e.what());
        }
    };

    sql::Connection* connection = nullptr;
    bool transaction_started = false;
    bool auto_commit_disabled = false;

    auto rollback = [&]() {
        if (connection != nullptr && transaction_started) {
            try {
                connection->rollback();
            } catch (const std::exception& e) {
                SPDLOG_ERROR("回滚算法配置事务失败: {}", e.what());
            }
            transaction_started = false;
        }
        if (connection != nullptr && auto_commit_disabled) {
            try {
                connection->setAutoCommit(true);
            } catch (const std::exception& e) {
                SPDLOG_ERROR("恢复数据库自动提交失败: {}", e.what());
            }
            auto_commit_disabled = false;
        }
    };

    try {
        const Json message_json = Json::parse(message);
        if (!message_json.is_object()) {
            throw std::invalid_argument("消息体必须是 JSON 对象");
        }

        const auto platanus = message_json.find("Platanus");
        if (platanus == message_json.end() || !platanus->is_array()) {
            throw std::invalid_argument("Platanus 必须是数组");
        }

        std::vector<AlgorithmConfig> configs;
        configs.reserve(platanus->size());
        for (std::size_t index = 0; index < platanus->size(); ++index) {
            configs.push_back(parse_algorithm_config(platanus->at(index), index));
        }

        connection = DBConnection::get_instance().get_connection();
        connection->setAutoCommit(false);
        auto_commit_disabled = true;
        transaction_started = true;

        {
            std::unique_ptr<sql::Statement> delete_statement(connection->createStatement());
            delete_statement->executeUpdate("DELETE FROM algorithm_config");
        }

        if (!configs.empty()) {
            // is_charging_off 按协议约定承载 ByRegion，其余列使用业务语义命名。
            std::unique_ptr<sql::PreparedStatement> insert_statement(
                connection->prepareStatement(
                    "INSERT INTO algorithm_config ("
                    "algorithm_type, algorithm_name, camera_channel, time_ranges, "
                    "start_date, end_date, is_charging_off, is_enabled"
                    ") VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));

            for (const auto& config : configs) {
                insert_statement->setInt(1, config.type);
                insert_statement->setString(2, config.name);
                insert_statement->setInt(3, config.channel);
                insert_statement->setString(4, config.time_ranges.dump());
                insert_statement->setString(5, config.start_date);
                insert_statement->setString(6, config.end_date);
                insert_statement->setInt(7, config.by_region);
                insert_statement->setInt(8, config.enabled);
                insert_statement->executeUpdate();
            }
        }

        connection->commit();
        transaction_started = false;
        try {
            connection->setAutoCommit(true);
            auto_commit_disabled = false;
        } catch (const std::exception& e) {
            SPDLOG_ERROR("算法配置已提交，但恢复数据库自动提交失败: {}", e.what());
        }

        send_ack(1, "");
        SPDLOG_INFO("算法配置保存成功，共 {} 个配置", configs.size());
    } catch (const sql::SQLException& e) {
        rollback();
        SPDLOG_ERROR("保存算法配置时发生数据库错误: {}", e.what());
        send_ack(0, "database error");
    } catch (const std::invalid_argument& e) {
        rollback();
        SPDLOG_WARN("算法配置参数不合法: {}", e.what());
        send_ack(0, e.what());
    } catch (const Json::exception& e) {
        rollback();
        SPDLOG_WARN("算法配置 JSON 不合法: {}", e.what());
        send_ack(0, "invalid json");
    } catch (const std::exception& e) {
        rollback();
        SPDLOG_ERROR("保存算法配置失败: {}", e.what());
        send_ack(0, "internal error");
    }
}

void AlgorithmConfigHandler::upload_algorithm_config(
    WebSocketClient& websocket_connect,
    const std::string& message) {
    try {
        const Json message_json = Json::parse(message);
        if (!message_json.is_object()) {
            throw std::invalid_argument("消息体必须是 JSON 对象");
        }

        auto* connection = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> statement(
            connection->prepareStatement(
                "SELECT algorithm_type, algorithm_name, camera_channel, time_ranges, "
                "start_date, end_date, is_charging_off, is_enabled "
                "FROM algorithm_config ORDER BY id"));
        std::unique_ptr<sql::ResultSet> result(statement->executeQuery());

        Json platanus = Json::array();
        std::size_t index = 0;
        while (result->next()) {
            Json time_ranges;
            try {
                const std::string stored_time_ranges = result->getString("time_ranges");
                time_ranges = Json::parse(stored_time_ranges);
            } catch (const Json::exception& e) {
                throw std::runtime_error(
                    "algorithm_config 第 " + std::to_string(index + 1) +
                    " 行的 time_ranges 不是有效 JSON: " + e.what());
            }
            if (!time_ranges.is_array()) {
                throw std::runtime_error(
                    "algorithm_config 第 " + std::to_string(index + 1) +
                    " 行的 time_ranges 必须是数组");
            }

            const Json row = {
                {"Type", result->getInt("algorithm_type")},
                {"Name", result->getString("algorithm_name")},
                {"Channel", result->getInt("camera_channel")},
                {"Time", Json{{"time", std::move(time_ranges)}}},
                {"StartDate", result->getString("start_date")},
                {"EndDate", result->getString("end_date")},
                {"ByRegion", result->getInt("is_charging_off")},
                {"Enable", result->getInt("is_enabled")}
            };
            platanus.push_back(to_json(parse_algorithm_config(row, index)));
            ++index;
        }

        const Json response = {
            {"Header", {
                {"CmdType", "RC_FuncConfig_Get"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Platanus", std::move(platanus)}
        };

        if (!websocket_connect.send_message(response.dump())) {
            SPDLOG_ERROR("算法配置查询成功，但响应发送失败");
            return;
        }
        SPDLOG_INFO("算法配置同步成功，共 {} 个配置", response.at("Platanus").size());
    } catch (const sql::SQLException& e) {
        SPDLOG_ERROR("查询算法配置时发生数据库错误: {}", e.what());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("查询算法配置失败: {}", e.what());
    }
}
