#pragma once
#include <string>

struct PowerConfig {
    int battery_low_pct;        // 电量下限 %
    int battery_high_pct;       // 电量上限 %
    std::string charge_strategy; // "charge_first"/"patrol_first"
    int battery_temp_warn_c;    // 电池温度阈值 ℃
    int battery_alarm_enabled;  // 电量告警开关 0/1
    int battery_temp_enabled;   // 电池温度告警开关 0/1
};