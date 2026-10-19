#pragma once
#include "mode_types.hpp"

class ModeRepository {
public:
    RobotMode load_mode();
    void save_mode(RobotMode mode);
};
