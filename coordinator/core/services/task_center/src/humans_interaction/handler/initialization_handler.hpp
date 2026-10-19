#pragma once
#include <string>
#include <zmq.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"

class InitializationController {
public:
    explicit InitializationController();
    void initialize_un_wait(const std::string& message, zmq::socket_t& zmq_socket);
    void initialize_wait(const std::string& message, zmq::socket_t& zmq_socket);
    void initialize_check_finish(zmq::socket_t& zmq_socket);
    void initialize_stop(zmq::socket_t& zmq_socket);

private:
    Config* config = Config::getInstance();
    zmq::context_t zmq_ctx{1};
    zmq::socket_t zmq_socket{zmq_ctx, zmq::socket_type::push};
    std::string base_url = "/gs-robot";
    std::unique_ptr<httplib::Client> http;
};