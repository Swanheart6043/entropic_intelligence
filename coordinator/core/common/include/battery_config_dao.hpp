#pragma once
#include "battery_config.hpp"

class PowerConfigDao {
public:
    // 读取单行配置(id=1);失败返回带默认值的结构体
    static PowerConfig get();
};