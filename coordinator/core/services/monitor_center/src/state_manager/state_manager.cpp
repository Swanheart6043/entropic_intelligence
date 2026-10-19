#include "state_manager.hpp"

GlobalStateManager::GlobalStateManager() : zmq_ctx(1), pub_socket(zmq_ctx, ZMQ_PUB), rep_socket(zmq_ctx, ZMQ_REP) {
    pub_socket.bind("tcp://*:7070"); // 广播服务
    rep_socket.bind("tcp://*:7071"); // 查询服务
}

GlobalStateManager::~GlobalStateManager() {

}

void GlobalStateManager::start() {
    running = true;
    initial_mode();
    mode_thread = std::thread([this](){subscribe_mode();});
    power_thread = std::thread([this](){subscribe_power();});    
    pub_thread = std::thread([this](){broadcast_state();});
    req_thread = std::thread([this](){handle_queries();});
    SPDLOG_INFO("全局状态管理已启动!");
}

void GlobalStateManager::stop() {
    running = false;
    if (mode_thread.joinable()) mode_thread.join();
    if (power_thread.joinable()) power_thread.join();
    if (pub_thread.joinable()) pub_thread.join();
    if (req_thread.joinable()) req_thread.join();
}

// ==================== 汇聚状态 ====================
void GlobalStateManager::initial_mode() {
    try {
        zmq::context_t sync_ctx(1);
        zmq::socket_t req_socket(sync_ctx, ZMQ_REQ);

        // 连接到task_center的mode查询服务
        req_socket.connect("tcp://localhost:5555");
        
        // 设置超时（避免task_center未启动时卡死）
        int timeout = 2000;  // 2秒超时
        int linger = 0;
        req_socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
        req_socket.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
        req_socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

        // 发送查询请求
        zmq::message_t request(0);
        req_socket.send(request, zmq::send_flags::none);
        
        // 接收响应
        zmq::message_t reply;
        auto result = req_socket.recv(reply, zmq::recv_flags::none);
        
        if (result) {
            std::string mode_str(static_cast<char*>(reply.data()), reply.size());        
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                global_state.mode = mode_str;
            }
            SPDLOG_INFO("同步初始mode成功: {}", mode_str);
        } else {
            SPDLOG_WARN("同步初始mode超时，使用默认值auto");
        }
        
    } catch (const zmq::error_t& e) {
        SPDLOG_WARN("同步初始mode失败: {}，使用默认值auto", e.what());
    }
}

// 订阅模式数据
void GlobalStateManager::subscribe_mode() {
    zmq::socket_t sub(zmq_ctx, ZMQ_SUB);
    const char* topic = "mode_info";
    sub.setsockopt(ZMQ_SUBSCRIBE, topic, strlen(topic));
    sub.connect("tcp://localhost:4096");

    while (running) {
        try {
            zmq::message_t topic, data;
            if (!sub.recv(topic, zmq::recv_flags::none)) continue;
            if (!sub.recv(data, zmq::recv_flags::none)) continue;
            
            std::string mode_str(static_cast<char*>(data.data()), data.size());
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                global_state.mode = mode_str;
            }
            SPDLOG_INFO("状态管理器: mode更新为 {}", mode_str);
            
        } catch (const std::exception& e) {
            SPDLOG_ERROR("订阅mode异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// 订阅电源主板数据
void GlobalStateManager::subscribe_power() {
    zmq::socket_t sub(zmq_ctx, ZMQ_SUB);
    sub.setsockopt(ZMQ_SUBSCRIBE, "", 0);
    sub.connect("tcp://127.0.0.1:6060");

    while (running) {
        try {
            zmq::message_t topic, data;
            if (!sub.recv(topic, zmq::recv_flags::none)) continue;
            if (!sub.recv(data, zmq::recv_flags::none)) continue;
            
            std::string msg(static_cast<char*>(data.data()), data.size());
            auto j = nlohmann::json::parse(msg);
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                if (j.contains("battery_percentage")) {
                    global_state.battery_percentage = j.at("battery_percentage").get<int>();
                }
                if (j.contains("is_stop")) {
                    global_state.is_stop = j.at("is_stop").get<bool>();
                }
                if (j.contains("is_auto_charge")) {
                    global_state.is_charging = j.at("is_auto_charge").get<bool>();
                }
                if (j.contains("valid")) {
                    global_state.valid = j.at("valid").get<int>();
                }
                if (j.contains("ew")) {
                    global_state.ew = j.at("ew").get<int>();
                }
                if (j.contains("sn")) {
                    global_state.sn = j.at("sn").get<int>();
                }
                if (j.contains("longitude")) {
                    global_state.longitude = j.at("longitude").get<int>();
                }
                if (j.contains("latitude")) {
                    global_state.latitude = j.at("latitude").get<int>();
                }
                if (j.contains("asl")) {
                    global_state.asl = j.at("asl").get<int>();
                }
                if (j.contains("speed")) {
                    global_state.speed = j.at("speed").get<int>();
                }
                if (j.contains("direction")) {
                    global_state.direction = j.at("direction").get<int>();
                }
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("订阅电源数据异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// ==================== 对外服务 ====================
void GlobalStateManager::broadcast_state() {
    while (running) {
        try {
            std::string state_json;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                state_json = global_state.to_json().dump();
            }
            
            std::string topic = "robot_state";
            zmq::message_t topic_msg(topic.begin(), topic.end());
            zmq::message_t data_msg(state_json.begin(), state_json.end());
            
            pub_socket.send(topic_msg, zmq::send_flags::sndmore);
            pub_socket.send(data_msg, zmq::send_flags::none);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (const std::exception& e) {
            SPDLOG_ERROR("广播状态异常: {}", e.what());
        }
    }
}

void GlobalStateManager::handle_queries() {
    while (running) {
        try {
            zmq::message_t request;
            if (!rep_socket.recv(request, zmq::recv_flags::none)) continue;
            
            // 返回完整状态
            std::string state_json;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                state_json = global_state.to_json().dump();
            }
            zmq::message_t reply(state_json.begin(), state_json.end());
            rep_socket.send(reply, zmq::send_flags::none);
            
        } catch (const std::exception& e) {
            SPDLOG_ERROR("处理查询异常: {}", e.what());
        }
    }
}

// ==================== 设置接口 ====================
void GlobalStateManager::set_global_state() {

}

void GlobalStateManager::set_doing(const std::string& doing) {
    std::lock_guard<std::mutex> lock(state_mutex);
    global_state.doing = doing;
}

// ==================== 查询接口 ====================
RobotState GlobalStateManager::get_global_state() const {
    std::lock_guard<std::mutex> lock(state_mutex);
    return global_state;
}

std::string GlobalStateManager::get_doing() const {
    return "";
}

// ==================== 判断接口 ====================
bool GlobalStateManager::is_auto_mode() {
    return global_state.mode == "auto";
}

bool GlobalStateManager::is_manual_mode() {
    return global_state.mode == "manual";
}