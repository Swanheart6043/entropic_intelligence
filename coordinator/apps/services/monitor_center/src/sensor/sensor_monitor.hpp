#pragma once
#include <zmq.hpp>
#include "config_parser.hpp"

void monitor_environmental(Config* config, std::shared_ptr<zmq::socket_t> zmq_socket);