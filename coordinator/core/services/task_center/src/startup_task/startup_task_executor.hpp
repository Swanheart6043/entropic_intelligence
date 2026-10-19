#pragma once
#include <thread>

class StartupTaskThread {
public:
    StartupTaskThread();
    ~StartupTaskThread();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};    
    std::thread coordinator_version_thread;
    std::thread navigation_version_thread;
    
    bool need_save = true;

    void check_coordinator_version();
    void check_navigation_version();
};