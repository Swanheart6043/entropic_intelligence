#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <zmq.hpp>
#include "config_parser.hpp"

class BasicInfoSender {
public:
	explicit BasicInfoSender();
	~BasicInfoSender();
	void start();    
    void stop();

private:
	static constexpr int INITIAL_RETRY_INTERVAL = 30; // 初始30秒
    static constexpr int MAX_RETRY_INTERVAL = 600; // 最大10分钟
    static constexpr int ZMQ_TIMEOUT_MS = 3000;  // 收发超时

    Config* config = Config::getInstance();
    
	zmq::context_t ctx{1};
	zmq::socket_t socket{ctx, zmq::socket_type::req};

    std::thread send_thread;
    std::mutex mutex;
    std::condition_variable cv;
    
    std::atomic<bool> stopped{false};
    std::atomic<int> retry_count{0};

    void send_loop();
    bool send_and_recv(); // 发送 + 等回复
    void reset_socket(); // 超时后重建 socket
};