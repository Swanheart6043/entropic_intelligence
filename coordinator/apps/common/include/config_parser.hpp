#pragma once
#include <string>
#include <fstream>
#include <iostream>

struct CloudOutboxPolicy {
    int max_jobs{512};
    int max_megabytes{256};
    int capture_ttl_hours{24};
    int dead_letter_ttl_hours{168};
    int dead_letter_max_jobs{128};
    int dead_letter_max_megabytes{32};
    int alarm_reserved_jobs{64};
    int alarm_reserved_megabytes{32};
    int cleanup_interval_seconds{60};
    int orphan_grace_seconds{600};
};

class Config {
public:
    static Config* getInstance();
    bool loadFromFile(const std::string& filename);
    std::string get_sn() const;
    std::string get_language() const;
    std::string get_websocket_url() const;
    std::string get_platform_http_server_url() const;
    std::string get_platform_http_base_path() const;
    std::string get_platform_http_api_path(const std::string& relative_path) const;
    std::string get_platform_rtsp_base_url() const;
    // 兼容旧调用；返回值与 get_platform_rtsp_base_url() 相同。
    std::string get_platform_rtsp_url() const;
    bool get_cloud_tls_verify() const;
    std::string get_cloud_ca_cert_path() const;
    std::string get_cloud_outbox_dir() const;
    int get_cloud_outbox_max_jobs() const;
    int get_cloud_outbox_max_megabytes() const;
    CloudOutboxPolicy get_cloud_outbox_policy() const;
    std::string get_db_connection() const;
    std::string get_db_user_name() const;
    std::string get_db_password() const;
    std::string get_schema() const;

private:
    std::string sn;
    std::string language;
    std::string platform_wss_url;
    std::string platform_http_url;
    std::string platform_http_base_path{"/robotboot"};
    std::string platform_rtsp_base_url;
    bool cloud_tls_verify{true};
    std::string cloud_ca_cert_path;
    std::string cloud_outbox_dir{"/var/lib/patrol_robot_h1/cloud_outbox"};
    CloudOutboxPolicy cloud_outbox_policy;
    std::string db_host;
    int db_port;
    std::string db_username;
    std::string db_password;
    std::string db_schema;

    static Config* instance;
    Config() = default;
};
