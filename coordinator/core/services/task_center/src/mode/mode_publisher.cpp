#include "mode_publisher.hpp"
#include <string>
#include <spdlog/spdlog.h>

ModeEventPublisher::ModeEventPublisher(zmq::context_t& context) : socket_(context, zmq::socket_type::pub) {
    socket_.bind("tcp://*:4096");
}

void ModeEventPublisher::publish(RobotMode mode) {
    try {
        const std::string topic = "mode_info";
        const std::string message = mode == RobotMode::AUTO ? "auto" : "manual";
        zmq::message_t topic_message(topic.begin(), topic.end());
        zmq::message_t data_message(message.begin(), message.end());

        socket_.send(topic_message, zmq::send_flags::sndmore);
        socket_.send(data_message, zmq::send_flags::none);

        SPDLOG_INFO("已发布模式变更到ZMQ: {}", message);
    } catch (const zmq::error_t& e) {
        SPDLOG_ERROR("ZMQ发送失败: {}", e.what());
    }
}
