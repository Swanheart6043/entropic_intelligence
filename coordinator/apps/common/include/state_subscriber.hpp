#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <spdlog/spdlog.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>

class StateSubscriber {
public:
    static StateSubscriber& getInstance() {
        static StateSubscriber instance;
        return instance;
    }
    StateSubscriber(const StateSubscriber&) = delete;
    StateSubscriber& operator=(const StateSubscriber&) = delete;
    
    void start() {
        initial();
        
        running = true;
        thread = std::thread([this]() {
            while (running) {
                try {
                    zmq::message_t topic, data;
                    if (!socket_sub.recv(topic, zmq::recv_flags::none)) continue;
                    if (!socket_sub.recv(data, zmq::recv_flags::none)) continue;
                    
                    std::string json_str(static_cast<char*>(data.data()), data.size());
                    auto j = nlohmann::json::parse(json_str);
                    {
                        std::lock_guard<std::mutex> lock(state_mutex);
                        cached_state = j;
                    }

                } catch (const std::exception& e) {
                    if (running) {
                        SPDLOG_ERROR("订阅状态异常: {}", e.what());
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
        });
    }
    
    void stop() {
        running = false;
        if (thread.joinable()) thread.join();
    }
    
    // 获取接口
    nlohmann::json get_full_state() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state;
    }

    std::string get_mode() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("mode", "");
    }

    int get_battery() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("battery_percentage", 0);
    }
    
    std::string get_current_map() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("current_map", "");
    }

    // 判断接口
    bool is_auto_mode() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("mode", "auto") == "auto";
    }

    bool is_charging() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("is_charging", false);
    }

    bool is_stop_pressed() const {
        std::lock_guard<std::mutex> lock(state_mutex);
        return cached_state.value("is_stop", false);
    }

private:
    zmq::context_t zmq_ctx;
    zmq::socket_t socket_sub;
    zmq::socket_t socket_req;
    nlohmann::json cached_state;
    mutable std::mutex state_mutex;
    std::thread thread;
    std::atomic<bool> running{false};
    
    StateSubscriber() : zmq_ctx(1), socket_sub(zmq_ctx, ZMQ_SUB), socket_req(zmq_ctx, ZMQ_REQ) {        
        socket_sub.setsockopt(ZMQ_SUBSCRIBE, "robot_state", strlen("robot_state"));
        socket_sub.connect("tcp://localhost:7070");
        socket_req.connect("tcp://localhost:7071");
        int timeout = 1000;
        socket_req.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    }
    ~StateSubscriber() {
        stop();
    }
    void initial() {
        try {
            zmq::message_t request(0);
            socket_req.send(request, zmq::send_flags::none);
            
            zmq::message_t reply;
            if (socket_req.recv(reply, zmq::recv_flags::none)) {
                std::string json_str(static_cast<char*>(reply.data()), reply.size());
                cached_state = nlohmann::json::parse(json_str);
                SPDLOG_INFO("同步初始状态成功");
            }
        } catch (const std::exception& e) {
            SPDLOG_WARN("同步初始状态失败: {}", e.what());
        }
    }
};