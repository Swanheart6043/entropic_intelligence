#pragma once
#include <string>
#include <zmq.hpp>
#include "config_parser.hpp"

class NavigationHandler {
public:
    explicit NavigationHandler(zmq::socket_t& zmq_socket);
    ~NavigationHandler();
    NavigationHandler(const NavigationHandler&) = delete;
    NavigationHandler& operator=(const NavigationHandler&) = delete;
    
    void move(const std::string& message);
    void go_to_point(const std::string& message);
    void control_to_point(const std::string& message);
    void run_line(const std::string& message);

private:
    Config* config = Config::getInstance();

    zmq::socket_t& zmq_socket_;
};