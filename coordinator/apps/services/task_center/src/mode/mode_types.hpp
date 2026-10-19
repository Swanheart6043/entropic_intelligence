#pragma once
#include <optional>

enum class RobotMode {
    MANUAL,
    AUTO
};

enum class AutoModeOperation {
    CONTINUINGFROMLAST,
    RESTART
};

class ModeObserver {
public:
    virtual ~ModeObserver() = default;
    virtual void change_mode(RobotMode new_mode, std::optional<AutoModeOperation> auto_mode_operation) = 0;
};
