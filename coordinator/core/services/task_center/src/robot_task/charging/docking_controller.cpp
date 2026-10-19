#include <thread>
#include <chrono>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "docking_controller.hpp"

DockingController::DockingController(zmq::context_t& ctx) : ctx_(ctx), socket_to_mcu(ctx, zmq::socket_type::push), socket_from_mcu(ctx, zmq::socket_type::sub) {
    socket_to_mcu.connect("tcp://127.0.0.1:6061");
}

void DockingController::reset_sub_socket() {
    socket_from_mcu = zmq::socket_t(ctx_, zmq::socket_type::sub);
    int hwm = 10;
    socket_from_mcu.setsockopt(ZMQ_RCVHWM, &hwm, sizeof(hwm));
    socket_from_mcu.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    socket_from_mcu.connect("tcp://127.0.0.1:6060");
}

void DockingController::send_message() {
    nlohmann::json message = {{"type", "charging"}};
    socket_to_mcu.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("已发送对桩命令, 等待结果");
}

// 开始对桩
DockingResult DockingController::start(std::function<bool()> navigate_func, std::atomic<bool>& cancel_flag) {
    for (int retry = 0; retry < MAX_RETRY; ++retry) {
        if (cancel_flag.load()) return DockingResult::CANCELLED;

        // 重试时重新导航
        if (retry > 0) {
            SPDLOG_INFO("开始第 {} 次对桩重试", retry);
            if (!navigate_func()) {
                SPDLOG_WARN("重试导航到充电点失败");
                continue;
            }
            if (cancel_flag.load()) return DockingResult::CANCELLED;
        }

        // 关键：每次对桩前换一个全新 SUB，丢弃历史积压
        reset_sub_socket();

        // 给订阅一点传播时间（顺便等惯性稳定），避免漏掉对桩后的头几条状态
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 发送对桩
        send_message();

        // 等待结果
        auto result = wait(cancel_flag);
        if (result == DockingResult::SUCCESS) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            slightly_lift_tire_toward_rear();
            return result;
        }
        if (result == DockingResult::CANCELLED) {
            return result;
        }
        // 超时或错误，继续重试
    }
    SPDLOG_ERROR("对桩失败，已重试{}次", MAX_RETRY);
    return DockingResult::TIMEOUT;
}

// 等待对桩结果
DockingResult DockingController::wait(std::atomic<bool>& cancel_flag) {
    auto start_time = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time);
        if (elapsed.count() >= TIMEOUT_SECONDS) {
            SPDLOG_WARN("对桩超时 ({} 秒)", TIMEOUT_SECONDS);
            return DockingResult::TIMEOUT;
        }
        if (cancel_flag.load()) return DockingResult::CANCELLED;

        // 收 topic 帧
        zmq::message_t topic_msg, data_msg;
        auto topic_result = socket_from_mcu.recv(topic_msg, zmq::recv_flags::dontwait);
        if (!topic_result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
            continue;
        }
        // 收配套 data 帧
        socket_from_mcu.recv(data_msg, zmq::recv_flags::none);
        std::string topic(static_cast<char*>(topic_msg.data()), topic_msg.size());
        if (topic != "charging_info") continue;
        std::string message(static_cast<char*>(data_msg.data()), data_msg.size());

        // 判断
        try {
            auto json = nlohmann::json::parse(message);
            // 全新 socket 下，true 一定是真实的当前状态
            if (json.at("is_auto_charge").get<bool>()) {
                SPDLOG_INFO("对桩成功！(耗时 {} 秒)", elapsed.count());
                return DockingResult::SUCCESS;
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("解析充电消息异常: {}", e.what());
            continue;
        }
    }
}

// 把轮胎向后方微微抬起，留个微弱间距，防止下次运动时刮倒限位杆
void DockingController::slightly_lift_tire_toward_rear() {
    nlohmann::json params = {{"speed", {{"linearSpeed", -0.01}, {"angularSpeed", 0}}}};
    httplib::Client http{"http://10.10.10.10:8080"};
    // 发一次只动一下就停，下一次动得再发
    auto response = http.Post("/gs-robot/cmd/move", params.dump(), "application/json");
    if (!response || response->status != 200) {
        SPDLOG_WARN("预留间距失败");
    } else {
        SPDLOG_INFO("预留间距成功");
    }
}