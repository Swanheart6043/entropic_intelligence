#pragma once
#include <atomic>
#include <string>

class DoorOpener {
public:
    static DoorOpener& getInstance() {
        static DoorOpener instance;
        return instance;
    }
    DoorOpener(const DoorOpener&) = delete;
    DoorOpener& operator=(const DoorOpener&) = delete;
    
    void start(const std::string& location, const std::string& type, int gap);
    void stop();

    void pause();
    void resume();
    
    bool is_running() const;
    bool is_pause() const;

private:
    DoorOpener();
    ~DoorOpener() noexcept;

    void send(const std::string& location, const std::string& type);
    void wait(int milliseconds);

    std::string customer_name;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelled{false};
    std::atomic<bool> paused{false};
};