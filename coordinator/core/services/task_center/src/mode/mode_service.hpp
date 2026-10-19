#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include "mode_types.hpp"

namespace zmq {
    class context_t;
}
class ModeBroadcaster;
class ModeEventPublisher;
class ModeQueryServer;
class ModeRepository;

class ModeService {
public:
    static ModeService& instance();
    ModeService(const ModeService&) = delete;
    ModeService& operator=(const ModeService&) = delete;
    ModeService(ModeService&&) = delete;
    ModeService& operator=(ModeService&&) = delete;

    ~ModeService();

    RobotMode get_current_mode() const;
    void register_observer(ModeObserver* observer);
    void unregister_observer(ModeObserver* observer);
    void switch_mode(RobotMode new_mode, std::optional<AutoModeOperation> auto_mode_operation);
    bool is_auto_mode() const;
    bool is_manual_mode() const;
    void stop();

private:
    RobotMode currentMode_;
    mutable std::mutex modeMutex_;
    std::vector<ModeObserver*> observers_;
    std::mutex observersMutex_;

    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<ModeRepository> repository_;
    std::unique_ptr<ModeEventPublisher> event_publisher_;
    std::unique_ptr<ModeQueryServer> query_server_;
    std::unique_ptr<ModeBroadcaster> broadcaster_;

    ModeService();
    void initialize();
    void notify_observers(RobotMode new_mode, std::optional<AutoModeOperation> auto_mode_operation);
};
