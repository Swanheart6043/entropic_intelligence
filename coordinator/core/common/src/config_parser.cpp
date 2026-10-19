#include "config_parser.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <INIReader.h>

namespace {
    std::string lower_ascii(std::string value) {
        std::transform(
            value.begin(), value.end(), value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
        );
        return value;
    }

    std::string get_url_scheme(const std::string& configured_url, const char* key_name) {
        const auto scheme_end = configured_url.find("://");
        if (scheme_end == std::string::npos || scheme_end == 0) {
            throw std::invalid_argument(std::string(key_name) + " must include a URL scheme");
        }
        const auto authority_start = scheme_end + 3;
        const auto authority_end = configured_url.find_first_of("/?#", authority_start);
        if (authority_start >= configured_url.size() || authority_end == authority_start) {
            throw std::invalid_argument(std::string(key_name) + " must include a host");
        }
        if (std::any_of(
                configured_url.begin(), configured_url.end(),
                [](unsigned char ch) { return std::isspace(ch) != 0; }
            )) {
            throw std::invalid_argument(std::string(key_name) + " must not include whitespace");
        }
        return lower_ascii(configured_url.substr(0, scheme_end));
    }

    void require_url_scheme(
        const std::string& configured_url,
        const char* key_name,
        const std::string& first_scheme,
        const std::string& second_scheme = ""
    ) {
        const std::string scheme = get_url_scheme(configured_url, key_name);
        if (scheme != first_scheme && (second_scheme.empty() || scheme != second_scheme)) {
            std::string message = std::string(key_name) + " scheme must be " + first_scheme;
            if (!second_scheme.empty()) message += " or " + second_scheme;
            throw std::invalid_argument(message);
        }
    }

    void normalize_url_scheme(std::string& configured_url, const char* key_name) {
        const auto scheme_end = configured_url.find("://");
        const std::string normalized_scheme = get_url_scheme(configured_url, key_name);
        configured_url.replace(0, scheme_end, normalized_scheme);
    }

    bool has_config_value(INIReader& reader, const char* section, const char* key) {
        static const std::string missing = "__patrol_robot_missing_config_value__";
        return reader.Get(section, key, missing) != missing;
    }

    void split_http_base_url(
        const std::string& configured_url,
        std::string& origin,
        std::string& base_path
    ) {
        require_url_scheme(configured_url, "http_base_url", "http", "https");
        if (configured_url.find('?') != std::string::npos ||
            configured_url.find('#') != std::string::npos) {
            throw std::invalid_argument("http_base_url must not include query or fragment");
        }
        const auto scheme_end = configured_url.find("://");
        const std::string scheme = lower_ascii(configured_url.substr(0, scheme_end));
        base_path.clear();
        const auto path_start = configured_url.find('/', scheme_end + 3);
        if (path_start == std::string::npos) {
            origin = scheme + configured_url.substr(scheme_end);
            return;
        }
        origin = scheme + configured_url.substr(scheme_end, path_start - scheme_end);
        base_path = configured_url.substr(path_start);
    }

    void normalize_base_path(std::string& path) {
        if (path.empty()) return;
        if (path.front() != '/') path.insert(path.begin(), '/');
        while (path.size() > 1 && path.back() == '/') path.pop_back();
    }

    void normalize_base_url(std::string& url) {
        while (!url.empty() && url.back() == '/') url.pop_back();
    }

    void replace_all(std::string& value, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        std::size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
}

Config* Config::getInstance() {
    static Config instance;
    return &instance;
    // if (instance == nullptr) {
    //     instance = new Config();
    // }
    // return instance;
}

bool Config::loadFromFile(const std::string& filename) {
    INIReader reader(filename);

    if (reader.ParseError() != 0) {
        if (reader.ParseError() < 0) {
            std::cerr << "无法打开配置文件: " << filename << std::endl;
        } else {
            std::cerr << "配置文件解析错误，行号: " << reader.ParseError() << std::endl;
        }
        return false;
    }
    
    try {
        // [device] section
        sn = reader.Get("device", "sn", "");
        language = reader.Get("device", "language", "zh");
        
        // [database] section
        db_host = reader.Get("database", "host", "localhost");
        db_port = reader.GetInteger("database", "port", 3306);
        db_username = reader.Get("database", "username", "");
        db_password = reader.Get("database", "password", "");
        db_schema = reader.Get("database", "schema", "");
        
        // [platform] section
        platform_wss_url.clear();
        platform_http_url.clear();
        platform_http_base_path = "/robotboot";
        platform_rtsp_base_url.clear();

        platform_wss_url = reader.Get("platform", "websocket_url", "");
        if (!platform_wss_url.empty()) {
            require_url_scheme(platform_wss_url, "websocket_url", "ws", "wss");
            normalize_url_scheme(platform_wss_url, "websocket_url");
            if (has_config_value(reader, "platform", "wss_server_ip") ||
                has_config_value(reader, "platform", "wss_server_port") ||
                has_config_value(reader, "platform", "wss_server_path") ||
                has_config_value(reader, "platform", "websocket_scheme")) {
                std::cerr << "警告: websocket_url 已配置，旧的 wss_server_* / websocket_scheme 字段将被忽略"
                          << std::endl;
            }
            replace_all(platform_wss_url, "{sn}", sn);
        } else {
            std::string wss_ip = reader.Get("platform", "wss_server_ip", "");
            int wss_port = reader.GetInteger("platform", "wss_server_port", 0);
            std::string wss_path = reader.Get("platform", "wss_server_path", "");
            std::string websocket_scheme = reader.Get("platform", "websocket_scheme", "ws");
            if (!wss_ip.empty() && wss_port > 0) {
                websocket_scheme = lower_ascii(websocket_scheme);
                if (websocket_scheme != "ws" && websocket_scheme != "wss") {
                    throw std::invalid_argument("websocket_scheme must be ws or wss");
                }
                std::stringstream ss;
                ss << websocket_scheme << "://" << wss_ip << ":" << wss_port << wss_path << sn;
                platform_wss_url = ss.str();
            }
        }

        platform_http_base_path = reader.Get("platform", "http_server_path", "/robotboot");
        std::string http_base_url = reader.Get("platform", "http_base_url", "");
        if (!http_base_url.empty()) {
            if (has_config_value(reader, "platform", "http_server_ip") ||
                has_config_value(reader, "platform", "http_server_port") ||
                has_config_value(reader, "platform", "http_server_path") ||
                has_config_value(reader, "platform", "http_server_scheme")) {
                std::cerr << "警告: http_base_url 已配置，旧的 http_server_* 字段将被忽略"
                          << std::endl;
            }
            split_http_base_url(http_base_url, platform_http_url, platform_http_base_path);
        } else {
            std::string http_ip = reader.Get("platform", "http_server_ip", "");
            int http_port = reader.GetInteger("platform", "http_server_port", 0);
            std::string http_scheme = reader.Get("platform", "http_server_scheme", "http");
            http_scheme = lower_ascii(http_scheme);
            if (http_scheme != "http" && http_scheme != "https") {
                throw std::invalid_argument("http_server_scheme must be http or https");
            }
            if (!http_ip.empty() && http_port > 0) {
                std::stringstream ss;
                ss << http_scheme << "://" << http_ip << ":" << http_port;
                platform_http_url = ss.str();
            }
        }
        normalize_base_path(platform_http_base_path);

        cloud_tls_verify = reader.GetBoolean("platform", "tls_verify", true);
        cloud_ca_cert_path = reader.Get("platform", "ca_cert_path", "");
        cloud_outbox_dir = reader.Get(
            "platform", "cloud_outbox_dir", "/var/lib/patrol_robot_h1/cloud_outbox"
        );
        const auto outbox_integer = [&](const char* key, int fallback, int minimum) {
            const long value = reader.GetInteger("platform", key, fallback);
            if (value < minimum || value > 1000000) {
                throw std::invalid_argument(std::string(key) + " is outside its allowed range");
            }
            return static_cast<int>(value);
        };
        auto& policy = cloud_outbox_policy;
        policy.max_jobs = outbox_integer("cloud_outbox_max_jobs", 512, 1);
        policy.max_megabytes = outbox_integer("cloud_outbox_max_megabytes", 256, 1);
        policy.capture_ttl_hours = outbox_integer("cloud_outbox_capture_ttl_hours", 24, 1);
        policy.dead_letter_ttl_hours = outbox_integer("cloud_outbox_dead_letter_ttl_hours", 168, 1);
        policy.dead_letter_max_jobs = outbox_integer("cloud_outbox_dead_letter_max_jobs", 128, 1);
        policy.dead_letter_max_megabytes = outbox_integer("cloud_outbox_dead_letter_max_megabytes", 32, 1);
        policy.alarm_reserved_jobs = outbox_integer(
            "cloud_outbox_alarm_reserved_jobs", std::min(64, policy.max_jobs / 4), 0);
        policy.alarm_reserved_megabytes = outbox_integer(
            "cloud_outbox_alarm_reserved_megabytes", std::min(32, policy.max_megabytes / 4), 0);
        policy.cleanup_interval_seconds = outbox_integer("cloud_outbox_cleanup_interval_seconds", 60, 1);
        policy.orphan_grace_seconds = outbox_integer("cloud_outbox_orphan_grace_seconds", 600, 1);
        if (policy.alarm_reserved_jobs >= policy.max_jobs ||
            policy.alarm_reserved_megabytes >= policy.max_megabytes) {
            throw std::invalid_argument("cloud outbox alarm reservations must be smaller than total limits");
        }
        
        platform_rtsp_base_url = reader.Get("platform", "rtsp_base_url", "");
        if (!platform_rtsp_base_url.empty()) {
            require_url_scheme(platform_rtsp_base_url, "rtsp_base_url", "rtsp", "rtsps");
            if (platform_rtsp_base_url.find('?') != std::string::npos ||
                platform_rtsp_base_url.find('#') != std::string::npos) {
                throw std::invalid_argument("rtsp_base_url must not include query or fragment");
            }
            normalize_url_scheme(platform_rtsp_base_url, "rtsp_base_url");
            if (has_config_value(reader, "platform", "rtsp_server_ip") ||
                has_config_value(reader, "platform", "rtsp_server_port")) {
                std::cerr << "警告: rtsp_base_url 已配置，旧的 rtsp_server_* 字段将被忽略"
                          << std::endl;
            }
        } else {
            std::string rtsp_ip = reader.Get("platform", "rtsp_server_ip", "");
            int rtsp_port = reader.GetInteger("platform", "rtsp_server_port", 0);
            if (!rtsp_ip.empty() && rtsp_port > 0) {
                std::stringstream ss;
                ss << "rtsp://" << rtsp_ip << ":" << rtsp_port;
                platform_rtsp_base_url = ss.str();
            }
        }
        normalize_base_url(platform_rtsp_base_url);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "配置文件加载失败: " << e.what() << std::endl;
        return false;
    }
}

std::string Config::get_sn() const {
    return sn;
}

std::string Config::get_language() const {
    return language;
}

std::string Config::get_websocket_url() const {
    return platform_wss_url;
}

std::string Config::get_platform_http_server_url() const {
    return platform_http_url;
}

std::string Config::get_platform_http_base_path() const {
    return platform_http_base_path;
}

std::string Config::get_platform_http_api_path(const std::string& relative_path) const {
    std::string base = platform_http_base_path;
    if (base == "/") base.clear();
    if (!base.empty() && base.back() == '/') base.pop_back();
    if (relative_path.empty()) return base.empty() ? "/" : base;
    return relative_path.front() == '/' ? base + relative_path : base + "/" + relative_path;
}

std::string Config::get_platform_rtsp_base_url() const {
    return platform_rtsp_base_url;
}

std::string Config::get_platform_rtsp_url() const {
    return get_platform_rtsp_base_url();
}

bool Config::get_cloud_tls_verify() const {
    return cloud_tls_verify;
}

std::string Config::get_cloud_ca_cert_path() const {
    return cloud_ca_cert_path;
}

std::string Config::get_cloud_outbox_dir() const {
    return cloud_outbox_dir;
}

int Config::get_cloud_outbox_max_jobs() const {
    return cloud_outbox_policy.max_jobs;
}

int Config::get_cloud_outbox_max_megabytes() const {
    return cloud_outbox_policy.max_megabytes;
}

CloudOutboxPolicy Config::get_cloud_outbox_policy() const {
    return cloud_outbox_policy;
}

std::string Config::get_db_connection() const {
    std::stringstream ss;
    ss << "tcp://" << db_host << ":" << db_port;
    return ss.str();
}

std::string Config::get_db_user_name() const {
    return db_username; 
}

std::string Config::get_db_password() const {
    return db_password; 
}

std::string Config::get_schema() const {
    return db_schema; 
}
