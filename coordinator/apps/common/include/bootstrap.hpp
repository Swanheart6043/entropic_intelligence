#pragma once
#include <string>

namespace patrol_robot {
    void init_crash_handler(const std::string& app_name);
    void init_logging(const std::string& app_name, int max_days = 7);
}