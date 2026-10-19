#pragma once
#include <zmq.hpp>
#include "broadcast_template.hpp"

class SosButtonMonitor {
public:
    explicit SosButtonMonitor();
    void start();

private:
    enum class SOSState {
        IDLE,           // 未按下
        PRESSED,        // 按下中
        WAIT_RELEASE,   // 等待松开（已判定为长按）
        WAIT_RECOVERY   // 新增：等待恢复（短按后的5秒等待期）
    };

    // 配置参数
    static constexpr int LONG_PRESS_MS = 3000; // 3秒判定为长按
    static constexpr int RECOVERY_DELAY_MS = 5000; // 5秒后恢复
    
    zmq::context_t ctx{1};
    zmq::socket_t socket_from_control_center{ctx, ZMQ_SUB};
    zmq::socket_t zmq_socket_task_center{ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_task_center_ipc{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};

    // SOS按钮状态管理
    SOSState sos_state_ = SOSState::IDLE;
    std::chrono::steady_clock::time_point sos_press_time_;
    std::chrono::steady_clock::time_point sos_release_time_; // 记录松开时间
    bool sos_long_press_triggered_ = false;
    
    BroadcastTemplate tpl_short_press_{"sos.short_press.greeting"};
    BroadcastTemplate tpl_long_press_{"sos.long_press.connecting"};

    // 处理SOS按下
    void handle_sos_button(bool current_state);
    // 处理短按
    void handle_sos_short_press();
    // 处理长按
    void handle_sos_long_press();
    // 发送暂停命令
    void send_pause_command();
    // 发送恢复命令
    void send_recovery_command();
    // 检测SOS超时, 未挂断
    void check_timeout();
};