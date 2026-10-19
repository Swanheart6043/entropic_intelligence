#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <zmq.hpp>
#include <boost/asio.hpp>
#include "../common/udp_client.hpp"
#include "./parser.hpp"

class CommunicationService {
public:
    CommunicationService(std::shared_ptr<boost::asio::io_context> io_ctx);
    ~CommunicationService();
    void start();
    void stop();

private:
    std::atomic<bool> running{false};
    std::thread worker_thread;

    UdpClient udp;
    zmq::context_t ctx{1};
	zmq::socket_t pull{ctx, zmq::socket_type::pull};

    Parser parser;

    void receive_message_loop(const std::string& message, const udp::endpoint& ep);
    void match_receive_message(uint16_t funid, std::vector<uint8_t>& data);

    void forward_message_loop();
    void match_forward_message(const std::string& message);

    bool unpacking(const uint8_t* buf, size_t bytes_recvd, uint16_t &out_funid, std::vector<uint8_t> &out_data, std::string &errReason);
    void send_frame(uint16_t funid, const std::vector<uint8_t>& data);
    void append_packet(std::vector<uint8_t>& v, uint16_t x);
};