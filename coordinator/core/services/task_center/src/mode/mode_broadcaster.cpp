#include "mode_broadcaster.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "broadcast_template.hpp"

ModeBroadcaster::ModeBroadcaster(zmq::context_t& context) : socket_to_edge_gateway_(context, zmq::socket_type::push) {
    socket_to_edge_gateway_.connect("ipc://@edge_gateway_push_pull");
}

void ModeBroadcaster::broadcast(RobotMode mode) {
    const bool automatic = mode == RobotMode::AUTO;
    BroadcastTemplate message_template{automatic ? "mode.automatic" : "mode.manual"};
    const nlohmann::json params = {
        {"command", "broadcast-single-text"},
        {"content", message_template.get_content(automatic ? "切换到自动模式" : "切换到手动模式")}
    };

    socket_to_edge_gateway_.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
    SPDLOG_INFO("切换到{}模式", automatic ? "自动" : "手动");
}
