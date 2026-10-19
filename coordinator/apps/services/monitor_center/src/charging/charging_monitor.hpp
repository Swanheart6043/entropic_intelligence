#pragma once
#include <zmq.hpp>
#include "./components/charge_target_monitor.hpp"
#include "./components/datetime_synchronizer.hpp"
#include "./components/temperature_monitor.hpp"

class ChargingMonitor {
public:
    ChargingMonitor();
    ~ChargingMonitor();
    void start();
    void stop();
    
private:
    std::atomic<bool> running{false};    
    zmq::context_t ctx{1};
    ChargeTargetMonitor charge_target_monitor;
    TimeSynchronizer time_synchronizer;
    TemperatureMonitor temperature_monitor;
};