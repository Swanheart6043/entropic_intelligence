#include <map>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include "config_parser.hpp"

class CaptureService {
public:
    CaptureService();
    ~CaptureService();
    void start();
    void stop();

private:
    void message_loop();

    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::rep};
    std::thread worker_thread;
    std::atomic<bool> running{false};
};
