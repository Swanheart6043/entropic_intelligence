#pragma once

// 子状态机单次任务运行(run_until_terminal)的结果，由 AutoTaskScheduler 消费。
enum class TaskRunOutcome {
    COMPLETED, // 正常完成(终态 execute 已完成上报，充电由顶层协调器决定)
    CANCELLED, // 被取消(终态 execute 已完成结束上报/清理)
    ERROR, // 错误终态(不重试, 交还调度器; ONCE 模式当日不再触发是既定业务行为)
    PAUSED, // 挂起: context 原样保留在状态机内存中, 等待调度器 resume/finalize/restart
    NOT_STARTED, // 巡逻/值守前检查未通过, 任务未真正开始(无上报产生)
};
