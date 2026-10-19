#include "mode_service.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <zmq.hpp>
#include "mode_broadcaster.hpp"
#include "mode_publisher.hpp"
#include "mode_query_server.hpp"
#include "mode_repository.hpp"

ModeService::ModeService() : currentMode_(RobotMode::AUTO), zmq_context_(std::make_unique<zmq::context_t>(1)), repository_(std::make_unique<ModeRepository>()), event_publisher_(std::make_unique<ModeEventPublisher>(*zmq_context_)), query_server_(std::make_unique<ModeQueryServer>(*zmq_context_, [this] { return get_current_mode(); })), broadcaster_(std::make_unique<ModeBroadcaster>(*zmq_context_)) {
    initialize();
    query_server_->start();
    // 仅用于验证 PUB/SUB 握手时序
    std::this_thread::sleep_for(std::chrono::seconds(2));
    event_publisher_->publish(currentMode_);
}

ModeService::~ModeService() {
    stop();
}

ModeService& ModeService::instance() {
    static ModeService instance;
    return instance;
}

void ModeService::register_observer(ModeObserver* observer) {
    if (observer == nullptr) return;

    std::lock_guard<std::mutex> lock(observersMutex_);
    if (std::find(observers_.begin(), observers_.end(), observer) == observers_.end()) {
        observers_.push_back(observer);
    }
}

void ModeService::unregister_observer(ModeObserver* observer) {
    std::lock_guard<std::mutex> lock(observersMutex_);
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

void ModeService::initialize() {
    currentMode_ = repository_->load_mode();
    event_publisher_->publish(currentMode_);
}

void ModeService::switch_mode(
    RobotMode new_mode,
    std::optional<AutoModeOperation> auto_mode_operation) {
    {
        std::lock_guard<std::mutex> lock(modeMutex_);
        currentMode_ = new_mode;
        repository_->save_mode(new_mode);
    }

    broadcaster_->broadcast(new_mode);
    notify_observers(new_mode, auto_mode_operation);
    event_publisher_->publish(new_mode);
}

void ModeService::notify_observers(RobotMode new_mode, std::optional<AutoModeOperation> auto_mode_operation) { 
    std::lock_guard<std::mutex> lock(observersMutex_);
    for (auto* observer : observers_) {
        observer->change_mode(new_mode, auto_mode_operation);
    }
}

void ModeService::stop() {
    query_server_->stop();
}

RobotMode ModeService::get_current_mode() const {
    std::lock_guard<std::mutex> lock(modeMutex_);
    return currentMode_;
}

bool ModeService::is_auto_mode() const {
    return get_current_mode() == RobotMode::AUTO;
}

bool ModeService::is_manual_mode() const {
    return get_current_mode() == RobotMode::MANUAL;
}
