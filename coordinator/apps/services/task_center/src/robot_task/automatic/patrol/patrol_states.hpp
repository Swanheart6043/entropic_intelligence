#include <spdlog/spdlog.h>
#include "patrol_state_machine_common.hpp"

class Patrol;

// ====================== 状态基类 =====================
class PatrolStateBase {
public:
    PatrolStateBase() = default;
    ~PatrolStateBase() = default;
    
    // 只在切换到该状态时执行一次
    virtual void on_enter(PatrolContext& ctx);
    // 状态停留期间持续/反复执行
    virtual PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) = 0;
    // 离开时执行一次
    virtual void on_exit(PatrolContext& ctx);
    // 获取状态名
    virtual std::string get_state_name() const = 0;
};

// ==================== 具体状态类 ====================
class IdleState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class QueryPlanState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class CheckBeforeDepartureState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class PreparePatrolState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class StartCycleState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class NavigatingState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class ExecutingActionState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class EndCycleState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class WaitingGapState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class CompletedState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class CancelledState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class PausedState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};

class ErrorState : public PatrolStateBase {
public:
    void on_enter(PatrolContext& ctx) override;
    PatrolEvent execute(PatrolContext& ctx, Patrol& patroller) override;
    std::string get_state_name() const override;
};