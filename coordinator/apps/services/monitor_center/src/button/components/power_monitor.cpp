#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "power_monitor.hpp"

PowerButtonMonitor::PowerButtonMonitor() {
    zmq_socket_power.setsockopt(ZMQ_SUBSCRIBE, "button_info", strlen("button_info"));
    zmq_socket_power.connect("tcp://127.0.0.1:6060");
}

void PowerButtonMonitor::start() {
    while (true) {
        zmq::message_t topic, zmq_message;    
        if (!zmq_socket_power.recv(topic, zmq::recv_flags::none)) continue;
        if (!zmq_socket_power.recv(zmq_message, zmq::recv_flags::none)) continue;
        std::string message_text(static_cast<char*>(zmq_message.data()), zmq_message.size());
        nlohmann::json message_json = nlohmann::json::parse(message_text);
        bool power_button = message_json.at("power_button").get<bool>();
        check_power_button(power_button);
    }
}

// 电源按钮按下时, 调用shoutdown
void PowerButtonMonitor::check_power_button(bool power_button) {
    if (!power_button) return;
    SPDLOG_INFO("检测到电源键按下！");
    std::system("sudo shutdown -h now");
}