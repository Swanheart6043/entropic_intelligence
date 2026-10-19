#pragma once
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <mutex>
#include <thread>
#include <atomic>

// 机器人全局状态
struct RobotState {
    std::string mode = "";
    std::string doing = "idle"; // "idle" | "going_to_charge" | "charging" | "patrol" | "duty" | "spinning_initialization" | "in_manual_tasks"
    
    // 导航相关
    std::string current_map = "";
    std::string location = "";

    // 电源相关
    int battery_percentage = 0;

    // GPS相关
    int valid = 0;
    int ew = 0;
    int sn = 0;
    int longitude = 0;
    int latitude = 0;
    int asl = 0;
    int speed = 0;
    int direction = 0;

    bool is_charging = false;
    bool is_location_lost = false;
    bool is_stay = false;
    bool is_stop = false;

    nlohmann::json to_json() const {
        return {
            {"mode", mode}, // 模式 - 重要
            {"is_charging", is_charging}, // 是否在充电 - 重要
            {"is_stay", is_stay}, // 是否在滞留 - 重要
            {"is_location_lost", is_location_lost}, // 是否定位丢失 - 重要
            {"battery_percentage", battery_percentage}, // 电量 - 重要
            {"is_stop", is_stop},
            {"current_map", current_map},
            {"doing", doing},
            {"gps", {
                {"valid", valid},
                {"ew", ew},
                {"sn", sn},
                {"longitude", longitude},
                {"latitude", latitude},
                {"asl", asl},
                {"speed", speed},
                {"direction", direction},
            }},
        };
    }
};

class GlobalStateManager {
public:
    static GlobalStateManager& getInstance() {
        static GlobalStateManager instance;
        return instance;
    }
    void start();
    void stop();
    
    void set_global_state();
    void set_doing(const std::string& doing);

    RobotState get_global_state() const;
    std::string get_doing() const;
    
    bool is_auto_mode();
    bool is_manual_mode();
    
private:
    std::atomic<bool> running{false};
    mutable std::mutex state_mutex;
    RobotState global_state;

    std::thread mode_thread;
    std::thread power_thread;
    std::thread pub_thread;
    std::thread req_thread;

    zmq::context_t zmq_ctx;
    zmq::socket_t pub_socket;
    zmq::socket_t rep_socket;
    
    GlobalStateManager();
    ~GlobalStateManager();

    void initial_mode();
    void subscribe_mode();
    void subscribe_power();
    void broadcast_state();
    void handle_queries();
};