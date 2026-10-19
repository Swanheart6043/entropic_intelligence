#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "action_executor.hpp"
#include "screenshotter.hpp"
#include "throughout_broadcaster.hpp"
#include "component/ptz_controller.hpp"
#include "component/mcu_controller.hpp"
#include "./door_opener/door_opener.hpp"
#include "./target_detector/target_detector.hpp"

ActionExecutor::ActionExecutor() {
    int linger = 1000;
	int sndtimeo = 1000;
    int sndhwm = 100;
    socket_to_edge_gateway.setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // 1秒，给消息足够的发送时间
    socket_to_edge_gateway.setsockopt(ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo)); // 设置发送超时，最多等1秒
    socket_to_edge_gateway.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm)); // 设置发送高水位，防止接收方不在时消息堆积
    socket_to_edge_gateway.connect("ipc://@edge_gateway_push_pull");
}

void ActionExecutor::clean() {
    
}

void ActionExecutor::execute_action_list(int action_group_id, const std::string& point_name, const std::string& report_uuid, std::atomic<bool>& cancel_flag, std::atomic<bool>& pause_flag) {
    try {
        // 加锁：确保同一时间只有一个动作组在执行
        std::lock_guard<std::mutex> lock(execution_mutex);

        // 接收外部取消 暂停状态
        should_cancel_.store(cancel_flag);
        should_pause_.store(pause_flag);

        // 检查动作组id
        if (action_group_id == 0) {
            SPDLOG_WARN("执行动作组时: 动作组id不能为空");
            return;
        }

        // 查询动作组
        auto list = query_action_list(action_group_id);
        if (list.empty()) {
            SPDLOG_WARN("没有查询到动作组, 退出");
            return;
        }

        // 逐个执行
        for (const auto& action : list) {
            if (!check_control_state()) {
                SPDLOG_INFO("动作序列被中断");
                return;
            }
            auto gap = action.action_wait != "0" ? std::stoi(action.action_wait) : 0;

            // 预置点
            if (action.action_type == "2") {
                execute_preset_point(action);
                interruptible_sleep(action.action_wait != "0" ? std::stoi(action.action_wait) : 0);
                continue;
            }
            // 抓拍
            if (action.action_type == "1") {
                Screenshotter screenshotter;
                screenshotter.execute_snapshot(action, point_name, report_uuid);
                interruptible_sleep(action.action_wait != "0" ? std::stoi(action.action_wait) : 0);
                continue;
            }
            // 目标检测
            if (action.action_type == "47") {
                TargetDetector::instance().start(action, point_name, report_uuid);
                continue;
            }
            // 灯光控制
            if (action.action_type == "32") {
                execute_light_control(action);
                interruptible_sleep(action.action_wait != "0" ? std::stoi(action.action_wait) : 0);
                continue;
            }
            // 语音播报
            if (action.action_type == "8") {
                execute_broadcast_text(action);
                interruptible_sleep(action.action_wait != "0" ? std::stoi(action.action_wait) : 0);
                continue;
            }
            // 全程循环播报, 贯穿巡逻周期，异步非阻塞执行
            if (action.action_type == "27") {
                throughout_broadcaster_text = action.action_value2;
                ThroughoutBroadcaster::getInstance().start(action.action_value2, gap);
                continue;
            }
            // 开门
            if (action.action_type == "41") {
                DoorOpener::getInstance().start(action.action_value1, action.action_value2, gap);
                continue;
            }
            // 音量控制
            if (action.action_type == "46") {
                // todo
            }
            // 环视
            if (action.action_type == "11") {
                execute_panorama(action);
                interruptible_sleep(action.action_wait != "0" ? std::stoi(action.action_wait) : 0);
                continue;
            }
            // 音频播报
            if (action.action_type == "3") {
                // todo
            }
            // 录音
            if (action.action_type == "30") {
                // todo
            }
        }
    } catch (std::exception &e) {
        SPDLOG_ERROR("执行动作组异常: {}", e.what());
    }
}

std::vector<ActionExecution> ActionExecutor::query_action_list(int action_group_id) {
    std::vector<ActionExecution> action_list;
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        std::string sql = R"(SELECT * FROM  plans_actions p WHERE p.action_group_id = ? ORDER BY p.id)";
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setInt(1, action_group_id);
        std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());
        
        while (res->next()) {
            ActionExecution action;
            action.action_group_id = res->getInt("action_group_id");
            action.action_id = res->getInt("action_id");
            action.action_type = res->getString("action_type");
            action.action_value1 = res->getString("action_value1");
            action.action_value2 = res->getString("action_value2");
            action.action_value3 = res->getString("action_value3");
            action.action_value4 = res->getString("action_value4");
            action.action_remark = res->getString("action_remark");
            action.action_wait = res->getString("action_wait");
            action_list.push_back(std::move(action));
        }

        return action_list;
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("数据库错误: {}, 错误代码: {}, SQL状态: {}", e.what(), e.getErrorCode(), e.getSQLState());
        return action_list;
    }
}

void ActionExecutor::execute_preset_point(const ActionExecution& action) {
    PTZController preset_position_executor;
    preset_position_executor.execute_preset_position(action.action_value1);
}

void ActionExecutor::execute_light_control(const ActionExecution& action) {
    McuController mcu_controller;
    if (action.action_value1 == "1") {
        if (action.action_value2 == "1") {
            mcu_controller.turn_on_warning_light();
        } else {
            mcu_controller.turn_off_warning_light();
        }
    }
    if (action.action_value1 == "0") {
        if (action.action_value2 == "1") {
            mcu_controller.turn_on_head_light();
        } else {
            mcu_controller.turn_off_head_light();
        }
    }
}

void ActionExecutor::execute_broadcast_text(const ActionExecution& action) {
    nlohmann::json params = {
        {"command", "broadcast-single-text"}, 
        {"content", action.action_value2}
    };
    socket_to_edge_gateway.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
}

void ActionExecutor::execute_body_rotation(const ActionExecution& action) {

}

void ActionExecutor::execute_panorama(const ActionExecution& action) {
    
}

bool ActionExecutor::check_control_state() {
    if (should_cancel_.load()) {
        SPDLOG_INFO("动作执行被取消");
        return false;
    }
    wait_if_paused();
    return true;
}

void ActionExecutor::wait_if_paused() {
    std::unique_lock<std::mutex> lock(pause_mutex_);
    if (should_pause_.load()) {
        SPDLOG_INFO("动作执行已暂停,等待恢复...");
    }
    while (should_pause_.load()) {
        pause_cv_.wait_for(lock, std::chrono::milliseconds(500));
        if (should_cancel_.load()) {
            SPDLOG_INFO("暂停期间收到取消请求");
            return;
        }
    }
}

void ActionExecutor::interruptible_sleep(int milliseconds) {
    if (milliseconds <= 0) return;
    int waited = 0;
    const int step = 500;
    while (waited < milliseconds) {
        if (!check_control_state()) {
            return;
        }
        int sleep_time = std::min(step, milliseconds - waited);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        waited += sleep_time;
    }
}