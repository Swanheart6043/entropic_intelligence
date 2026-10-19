#pragma once

namespace plan {
    enum class PlanType {
        PATROL, // 巡检
        DUTY // 值守
    };
    enum class ExecutionMode {
        ONCE, // 一次性执行
        REPEAT // 重复执行
    };
}