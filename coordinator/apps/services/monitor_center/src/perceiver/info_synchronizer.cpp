#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "info_synchronizer.hpp"

BasicInfoSender::BasicInfoSender() {
    reset_socket();
}

BasicInfoSender::~BasicInfoSender() {
    stop();
}

void BasicInfoSender::reset_socket() {
    socket.close();
    socket = zmq::socket_t(ctx, zmq::socket_type::req);

    int timeout = ZMQ_TIMEOUT_MS;
    int linger = 1000;
    int sndhwm = 100;
    socket.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket.connect("tcp://10.10.10.30:2430");
}

void BasicInfoSender::start() {
    if (send_thread.joinable()) {
        SPDLOG_WARN("发送线程已经在运行");
        return;
    }
    stopped = false;
    retry_count = 0;
    send_thread = std::thread(&BasicInfoSender::send_loop, this);
}

void BasicInfoSender::stop() {
    stopped = true;
    cv.notify_all();
    if (send_thread.joinable()) {
        send_thread.join();
    }
}

void BasicInfoSender::send_loop() {
    SPDLOG_INFO("基础信息同步流程启动...");
    int wait_time = INITIAL_RETRY_INTERVAL;

    while (!stopped) {
        retry_count++;
        SPDLOG_INFO("尝试发送基础信息 [第{}次]", retry_count.load());

        if (send_and_recv()) {
            SPDLOG_INFO("基础信息同步成功！共尝试{}次", retry_count.load());
            return;  // 成功，线程退出
        }

        SPDLOG_WARN("同步失败，{}秒后重试...", wait_time);
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::seconds(wait_time), [this] {
            return stopped.load();
        });
        wait_time = std::min(wait_time * 2, MAX_RETRY_INTERVAL);
    }

    SPDLOG_WARN("基础信息同步流程被终止");
}

bool BasicInfoSender::send_and_recv() {
    try {
        using json = nlohmann::json;
        json payload = {
            {"cmd", "basic_info_set"},
            {"sn", config->get_sn()},
            // 兼容现有感知中心 basic_info_set 协议；协调器自身不再使用该地址发起云端请求。
            {"web_http_url", config->get_platform_http_server_url()},
            {"db_connection", config->get_db_connection()},
            {"db_user_name", config->get_db_user_name()},
            {"db_password", config->get_db_password()},
            {"schema", config->get_schema()}
        };

        // 发送
        auto send_result = socket.send(zmq::buffer(payload.dump()), zmq::send_flags::none);
        if (!send_result) {
            SPDLOG_WARN("ZMQ send 超时");
            reset_socket();  // REQ 状态机坏了，必须重建
            return false;
        }

        // 等回复（REQ/REP 闭环）
        zmq::message_t reply;
        auto recv_result = socket.recv(reply, zmq::recv_flags::none);
        if (!recv_result) {
            SPDLOG_WARN("ZMQ recv 超时，重建 socket");
            reset_socket();
            return false;
        }

        // 解析回复
        std::string reply_str(static_cast<char*>(reply.data()), reply.size());
        auto resp = json::parse(reply_str, nullptr, false);
        if (resp.is_discarded() || !resp.value("ok", false)) {
            SPDLOG_WARN("对端返回异常: {}", reply_str);
            return false;
        }
        return true;
    } catch (const zmq::error_t& e) {
        SPDLOG_ERROR("ZMQ异常: {}", e.what());
        reset_socket();
        return false;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("发送异常: {}", e.what());
        return false;
    }
}
