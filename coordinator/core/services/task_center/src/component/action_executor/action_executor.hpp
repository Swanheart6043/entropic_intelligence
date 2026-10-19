#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <zmq.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"

struct ActionExecution {
    int action_group_id;
    int action_id;
    std::string action_type;
    std::string action_value1;
    std::string action_value2;
    std::string action_value3;
    std::string action_value4;
    std::string action_remark;
    std::string action_wait;
};

class ActionExecutor {
public:
    static ActionExecutor& getInstance() {
        static ActionExecutor instance;
        return instance;
    }
    // 删除拷贝和赋值
    ActionExecutor(const ActionExecutor&) = delete;
    ActionExecutor& operator=(const ActionExecutor&) = delete;

    void execute_action_list(
        int action_group_id,
        const std::string& point_name,
        const std::string& report_uuid,
        std::atomic<bool>& cancel_flag,
        std::atomic<bool>& pause_flag
    );

private:
    ActionExecutor();
    ~ActionExecutor() = default;

    std::mutex execution_mutex;

    Config* config = Config::getInstance();
    std::unique_ptr<httplib::Client> http;
    
    zmq::context_t ctx{1};
    zmq::socket_t socket_to_edge_gateway{ctx, zmq::socket_type::push};

    // 控制状态
    std::atomic<bool> should_cancel_;
    std::atomic<bool> should_pause_;

    std::mutex pause_mutex_;
    std::condition_variable pause_cv_;
    std::string throughout_broadcaster_text;

    // 在每个动作执行前/后检查状态
    bool check_control_state();
    void wait_if_paused();
    void interruptible_sleep(int milliseconds);
    /**
     * 查询动作组
     * @param action_group_id
     */
    std::vector<ActionExecution> query_action_list(int action_group_id);
    /**
     * 执行预置位
     * @param action
     */
    void execute_preset_point(const ActionExecution& action);
    /**
     * 执行环视
     * @param action
     */
    void execute_panorama(const ActionExecution& action);
    /**
     * 执行灯光控制
     * @param action
     */
    void execute_light_control(const ActionExecution& action);
    /**
     * 语音播报
     * @param action
     */
    void execute_broadcast_text(const ActionExecution& action);
    /**
     * 执行转动方向
     * @param action
     */
    void execute_body_rotation(const ActionExecution& action);

    void clean();
};