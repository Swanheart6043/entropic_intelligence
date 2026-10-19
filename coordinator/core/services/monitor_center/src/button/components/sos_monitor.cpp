#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "sos_monitor.hpp"

SosButtonMonitor::SosButtonMonitor() {
    socket_from_control_center.setsockopt(ZMQ_SUBSCRIBE, "button_info", strlen("button_info"));
    socket_from_control_center.connect("tcp://127.0.0.1:6060");
    zmq_socket_task_center.connect("tcp://127.0.0.1:4004");
    zmq_socket_task_center_ipc.connect("tcp://127.0.0.1:8223");
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
}

void SosButtonMonitor::start() {
    while (true) {
        zmq::message_t topic, zmq_message;    
        if (!socket_from_control_center.recv(topic, zmq::recv_flags::none)) continue;
        if (!socket_from_control_center.recv(zmq_message, zmq::recv_flags::none)) continue;
        std::string message_text(static_cast<char*>(zmq_message.data()), zmq_message.size());
        nlohmann::json message_json = nlohmann::json::parse(message_text);
        bool sos_button = message_json.at("sos_button").get<bool>();
        handle_sos_button(sos_button);
    }
}

void SosButtonMonitor::handle_sos_button(bool sos_button) {
    auto now = std::chrono::steady_clock::now();
    switch (sos_state_) {
        case SOSState::IDLE: {
            // 检测到按下
            if (sos_button) {
                SPDLOG_INFO("SOS被按下");
                sos_state_ = SOSState::PRESSED;
                sos_press_time_ = now;
                sos_long_press_triggered_ = false;
            }
            break;
        }
        case SOSState::PRESSED: {
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - sos_press_time_).count();
            // 先判断长按阈值：按住期间一旦达到立即触发
            if (sos_button && duration >= LONG_PRESS_MS && !sos_long_press_triggered_) {
                SPDLOG_INFO("SOS long press threshold reached ({}ms >= {}ms)", duration, LONG_PRESS_MS);
                handle_sos_long_press();
                sos_long_press_triggered_ = true;
                sos_state_ = SOSState::WAIT_RELEASE;
                break;
            }
            // 松开处理
            if (!sos_button) {
                if (duration < LONG_PRESS_MS) {
                    // 短按
                    handle_sos_short_press();
                    sos_state_ = SOSState::WAIT_RECOVERY;
                    sos_release_time_ = now;
                } else {
                    // 已经按满阈值但还没触发（轮询漏掉的兜底）
                    if (!sos_long_press_triggered_) {
                        handle_sos_long_press();
                    }
                    sos_state_ = SOSState::IDLE;
                }
            }
            break;
        }
        case SOSState::WAIT_RELEASE: {
            // 松开了
            if (!sos_button) {
                auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - sos_press_time_).count();
                SPDLOG_INFO("SOS button released after long press, total duration: {}ms", total_duration);
                sos_state_ = SOSState::IDLE;
            }
            break;
        }
        case SOSState::WAIT_RECOVERY: {
            if (sos_button) {
                // 在5秒等待期内又按下了
                sos_state_ = SOSState::PRESSED;
                sos_press_time_ = now;
                sos_long_press_triggered_ = false;
            } else {
                // 检查是否到了5秒
                auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - sos_release_time_).count();
                if (wait_duration >= RECOVERY_DELAY_MS) {
                    send_recovery_command();
                    sos_state_ = SOSState::IDLE;
                }
            }
            break;
        }
    }
}

void SosButtonMonitor::handle_sos_short_press() {
    SPDLOG_INFO("检测到短按");
    nlohmann::json broadcast_params = {
        {"command", "broadcast-single-text"}, 
        {"content", tpl_short_press_.get_content("您好,如需帮助,请长按s o s按键3秒以上")}
    };
    socket_to_edge_gateway.send(zmq::buffer(broadcast_params.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("通知android播放声音: {}", broadcast_params.dump());
    
    nlohmann::json message = {{"command", "sos-pressed"}, {"content", ""}};
    zmq_socket_task_center_ipc.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("通知任务中心暂停: {}", message.dump());
}

void SosButtonMonitor::handle_sos_long_press() {
    SPDLOG_INFO("检测到长按");
    nlohmann::json ptz_message = {
        {"Header", {{"CmdType", "RC_Robot_PTZ_Ctrl"}}},
        {"Robot_PTZ_Ctrl", {{"Value", "12"}, {"unCtrlValue", "12"}}}
    };
    zmq_socket_task_center.send(zmq::buffer(ptz_message.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("云台转动到sos方向: {}", ptz_message.dump());
    
    nlohmann::json sos_params = {{"command", "sos"}, {"content", ""}};
    socket_to_edge_gateway.send(zmq::buffer(sos_params.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("通知android执行sos流程: {}", sos_params.dump());

    nlohmann::json broadcast_params = {
        {"command", "broadcast-single-text"},
        {"content", tpl_long_press_.get_content("正在接通请稍后")}
    };
    socket_to_edge_gateway.send(zmq::buffer(broadcast_params.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("通知android播放声音: {}", broadcast_params.dump());
}

void SosButtonMonitor::send_pause_command() {
    SPDLOG_INFO("通知任务中心SOS暂停");
    nlohmann::json message = {
        {"command", "sos-pressed"}, 
        {"content", ""}
    };
    zmq_socket_task_center_ipc.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
}

void SosButtonMonitor::send_recovery_command() {
    SPDLOG_INFO("通知任务中心SOS恢复");
    nlohmann::json message = {
        {"command", "sos-recovery"}, 
        {"content", ""}
    };
    zmq_socket_task_center_ipc.send(
        zmq::buffer(message.dump()), 
        zmq::send_flags::dontwait
    );
}