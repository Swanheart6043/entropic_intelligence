#include <thread>
#include <zmq.hpp>

class EdgeWebsocketServer {
public:
    EdgeWebsocketServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx);
    ~EdgeWebsocketServer();
    void start();
    void stop();

private:
    void websocket_loop();
    void state_loop();
    
    std::atomic<bool> running{false};
    std::thread worker_thread;
    std::thread state_thread;

    nlohmann::json& robot_info_;
    std::mutex& robot_info_mtx_;
};