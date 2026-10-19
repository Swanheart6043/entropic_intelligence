#pragma once
#include <spdlog/spdlog.h>
#include "duty_state_machine_common.hpp"
#include "duty.hpp"

namespace duty_task {
    class DutyStateBase {
    public:
        virtual ~DutyStateBase() = default;
        virtual void on_enter(DutyContext& ctx);
        virtual DutyEvent execute(DutyContext& ctx, Duty& duty) = 0;
        virtual void on_exit(DutyContext& ctx);
        virtual std::string get_state_name() const = 0;
    };

    class IdleState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& duty) override;
        std::string get_state_name() const override;
    };

    class QueryPlanState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class CheckConditionState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class PreparePatrolState : public DutyStateBase {
    public:
        void on_enter(DutyContext& ctx) override;
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class NavigatingState : public DutyStateBase {
    public:
        void on_enter(DutyContext& ctx) override;
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class ExecutingActionState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class WaitingGapState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class CompletedState : public DutyStateBase {
    public:
        void on_enter(DutyContext& ctx) override;
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class CancelledState : public DutyStateBase {
    public:
        void on_enter(DutyContext& ctx) override;
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };

    class ErrorState : public DutyStateBase {
    public:
        DutyEvent execute(DutyContext& ctx, Duty& patroller) override;
        std::string get_state_name() const override;
    };
}