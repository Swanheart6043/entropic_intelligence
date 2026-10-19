#pragma once
#include <atomic>
#include <zmq.hpp>
#include "component/mcu_controller.hpp"
#include "component/ptz_controller.hpp"

struct AlarmAction {
    std::string alarm_type;
    std::string action_type;
    std::string value1;
    std::string value2;
    int wait_ms;
};

inline void to_json(nlohmann::json& j, const AlarmAction& a) {
    j = nlohmann::json{
        {"alarm_type", a.alarm_type},
        {"action_type", a.action_type},
        {"value1", a.value1},
        {"value2", a.value2},
        {"wait_ms", a.wait_ms}
    };
}

inline void from_json(const nlohmann::json& j, AlarmAction& a) {
    a.alarm_type = j.value("alarm_type", "");
    a.action_type = j.value("action_type", "");
    a.value1 = j.value("value1", "");
    a.value2 = j.value("value2", "");
    a.wait_ms = j.value("wait_ms", 0);
}

class AlarmActionHandler {
public:
    AlarmActionHandler();
    ~AlarmActionHandler();

    static void handle_async(const std::string& message);
    void cancel();
    void resume();
    bool is_progress();

private:
    inline static std::atomic<bool> running{false};
    std::mutex cv_m;
    std::condition_variable cv;
    
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};

    McuController mcu_controller;
    PTZController ptz_controller;

    void handle(const std::string& message);
    void handle_broadcasts(AlarmAction action);
    void handle_warning_light(AlarmAction action);
    void handle_looking_around(AlarmAction action);
    void handle_sos(AlarmAction action);
    void handle_pause_move(AlarmAction action);
    void handle_resume_move(AlarmAction action);

    void sleeping(int ms);
};