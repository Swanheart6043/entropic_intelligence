#include <spdlog/spdlog.h>
#include "yue_chu_an_xun.hpp"

YueChuAnXun::YueChuAnXun() {
    http.set_connection_timeout(5);
    http.set_read_timeout(5);
}

void YueChuAnXun::control(const std::string& location, const std::string& type) {
    try {
        const std::string path = "/saccserver/plcControllerLamp/robot/patrol/" + location + "/" + type;
        auto response = http.Get(path, headers);
        if (!response) {
            SPDLOG_WARN("远程开门请求失败 path={}, err={}", path, httplib::to_string(response.error()));
            return;
        }
        if (response->status != 200) {
            SPDLOG_WARN("远程开门请求失败 path={}, status={}, body={}", path, response->status, response->body);
            return;
        }
        SPDLOG_INFO("远程开门请求成功");

    } catch (std::exception& e) {
        SPDLOG_ERROR("远程开门请求异常: {}", e.what());
    }
}