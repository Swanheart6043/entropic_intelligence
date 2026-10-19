#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "sole.hpp"
#include "config_parser.hpp"
#include "sensor_monitor.hpp"
#include "../common/utility.hpp"

void monitor_environmental(Config* config, std::shared_ptr<zmq::socket_t> zmq_socket) {
    using namespace nlohmann;

    // 高温
    if (true) {
        
    }

    sole::uuid id = sole::uuid4();
    std::string uuid_string = id.str();

    nlohmann::json message = {
        {"Header", {
            {"CmdType", "Robot_Alarm_info_Push"},
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"PatrolAlarmInfo", {
            {"time", format_now_date_to_string()},
            {"alarmType", 207},
            {"alarmValue", "80"},
            {"uuid", uuid_string}
        }}
    };

    SPDLOG_INFO("高温告警: {}", message.dump());
    zmq_socket->send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
}