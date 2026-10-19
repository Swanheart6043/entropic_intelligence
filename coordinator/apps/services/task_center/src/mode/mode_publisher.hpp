#pragma once
#include <zmq.hpp>
#include "mode_types.hpp"

class ModeEventPublisher {
public:
    explicit ModeEventPublisher(zmq::context_t& context);
    void publish(RobotMode mode);

private:
    zmq::socket_t socket_;
};
