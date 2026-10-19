#pragma once
#include <atomic>
#include "robot_task/task_types.hpp"

// 手动运动执行器依赖的最小所有权端口。执行器只通过该抽象申请/释放机器人，不依赖具体协调器实现。
class ManualMotionOwnership {
public:
    virtual ~ManualMotionOwnership() = default;
    virtual bool prepare_manual_motion() = 0;
    virtual bool acquire_manual_motion(const std::atomic<bool>& requester_running, const std::atomic<bool>& requester_cancelled) = 0;
    virtual void release_manual_motion() = 0;
    virtual RobotTaskState current_state() const = 0;
};
