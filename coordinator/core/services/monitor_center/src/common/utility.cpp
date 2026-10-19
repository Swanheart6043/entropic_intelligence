#include <chrono>
#include <sstream>
#include <iomanip>
#include "utility.hpp"

std::string format_now_date_to_string() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
    return ss.str();
}