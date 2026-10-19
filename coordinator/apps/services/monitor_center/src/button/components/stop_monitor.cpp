#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "config_parser.hpp"
#include "stop_monitor.hpp"

StopButtonMonitor::StopButtonMonitor() {
    zmq_socket_power.setsockopt(ZMQ_SUBSCRIBE, "button_info", strlen("button_info"));
    zmq_socket_power.connect("tcp://127.0.0.1:6060");
    zmq_socket_web.connect("tcp://127.0.0.1:2007");
}

void StopButtonMonitor::start() {
    while (true) {
        zmq::message_t topic, zmq_message;
        if (!zmq_socket_power.recv(topic, zmq::recv_flags::none)) continue;
        if (!zmq_socket_power.recv(zmq_message, zmq::recv_flags::none)) continue;
        std::string message_text(static_cast<char*>(zmq_message.data()), zmq_message.size());
        nlohmann::json message_json = nlohmann::json::parse(message_text);
        bool stop_button = message_json.at("stop_button").get<bool>();
        check_stop_button(stop_button);
    }
}

// 急停按钮按下时, 判断有无超过5分钟，超过告警
void StopButtonMonitor::check_stop_button(bool stop_button) {
    if (!stop_button) return;
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "40096"}}}
    };
    zmq_socket_web.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
}