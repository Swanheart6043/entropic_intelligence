#pragma once
#include <zmq.hpp>
#include "mode_types.hpp"

class ModeBroadcaster {
public:
    explicit ModeBroadcaster(zmq::context_t& context);
    void broadcast(RobotMode mode);

private:
    zmq::socket_t socket_to_edge_gateway_;
};
