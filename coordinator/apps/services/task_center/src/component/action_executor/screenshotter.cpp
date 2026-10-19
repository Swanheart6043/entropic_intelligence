#include <chrono>
#include <iomanip>
#include <sstream>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "screenshotter.hpp"

Screenshotter::Screenshotter() : zmq_ctx(1) {

}

void Screenshotter::execute_snapshot(const ActionExecution& action, const std::string& point_name, const std::string& report_uuid) {
    SPDLOG_INFO("开始执行抓拍: {}", action.action_type);

    zmq::socket_t zmq_socket_req(zmq_ctx, zmq::socket_type::req);
    int timeout = 5000;
    zmq_socket_req.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    zmq_socket_req.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_socket_req.connect("ipc://@audio_video_req");
    
    nlohmann::json params = {{"type", "screenshot"}};
    auto send_result = zmq_socket_req.send(zmq::buffer(params.dump()), zmq::send_flags::none);
    if (!send_result) {
        SPDLOG_ERROR("发送截图请求失败");
        return;
    }

    zmq::message_t reply;
    auto recv_result = zmq_socket_req.recv(reply, zmq::recv_flags::none);
    if (!recv_result) {
        SPDLOG_ERROR("接收截图响应超时");
        return;
    }
    std::string image(static_cast<char*>(reply.data()), reply.size());

    const int detection_type = action.action_value1.empty() ? 0 : std::stoi(action.action_value1);
    std::string gateway_error;
    if (!cloud_gateway_.enqueue_capture_report(
            image.data(),
            image.size(),
            point_name,
            action.action_id,
            report_uuid,
            detection_type,
            format_now_date_to_string(),
            &gateway_error)) {
        SPDLOG_ERROR("抓拍上报写入web_gateway队列失败: {}", gateway_error);
        return;
    }
    SPDLOG_INFO("抓拍上报已写入web_gateway持久化队列: action_id={}", action.action_id);
}

std::string Screenshotter::format_now_date_to_string() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
    return ss.str();
}
