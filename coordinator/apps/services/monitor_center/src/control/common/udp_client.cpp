#include <spdlog/spdlog.h>
#include "udp_client.hpp"

UdpClient::UdpClient(std::shared_ptr<boost::asio::io_context> ctx) : io_ctx(std::move(ctx)), strand(boost::asio::make_strand(io_ctx->get_executor())) {

}

UdpClient::~UdpClient() {
    stop();
}

bool UdpClient::init(const std::string& remote_ip, uint16_t remote_port, uint16_t local_port) {
    try {
        default_target = udp::endpoint(boost::asio::ip::make_address(remote_ip), remote_port);
        socket = std::make_unique<udp::socket>(*io_ctx, udp::endpoint(udp::v4(), local_port));
        connected = true;
        return true;
    } catch (const std::exception& e) {
        SPDLOG_ERROR("UDP 初始化失败: {}", e.what());
        connected = false;
        return false;
    }
}

void UdpClient::start() {
    if (running.exchange(true)) return; // 已经在跑了
    // 在 strand 上提交接收任务,保证后续所有回调串行
    boost::asio::post(strand, [this]{ startReceive(); });
}

void UdpClient::stop() {
    if (!running.exchange(false)) return;
    boost::asio::post(strand, [this]{
        if (socket && socket->is_open()) {
            boost::system::error_code ec;
            socket->close(ec);
            if (ec) SPDLOG_WARN("socket close 警告: {}", ec.message());
        }
        connected = false;
    });
}

void UdpClient::startReceive() {
    if (!running || !socket) return;
    socket->async_receive_from(
        boost::asio::buffer(recv_buffer),
        peer_endpoint,
        boost::asio::bind_executor(
            strand,
            [this](boost::system::error_code ec, std::size_t bytes_recvd) {
                if (ec) {
                    if (ec != boost::asio::error::operation_aborted) {
                        SPDLOG_WARN("UDP 接收错误: {}", ec.message());
                    }
                    if (running) startReceive();
                    return;
                }
                if (bytes_recvd > 0) {
                    received_count++;
                    if (message_cb) {
                        try {
                            std::string message(recv_buffer.data(), bytes_recvd);
                            message_cb(message, peer_endpoint);
                        } catch (const std::exception& e) {
                            SPDLOG_ERROR("消息回调异常: {}", e.what());
                        }
                    }
                }
                if (running) startReceive();
            }
        )
    );
}

void UdpClient::sendMessage(const std::string& message) {
    sendMessageTo(message, default_target);
}

void UdpClient::sendMessageTo(const std::string& message, const udp::endpoint& endpoint) {
    if (!connected || !socket) return;

    // 关键: 消息必须在异步完成前保持存活
    auto msg = std::make_shared<std::string>(message);
    socket->async_send_to(
        boost::asio::buffer(*msg),
        endpoint,
        boost::asio::bind_executor(
            strand, 
            [this, msg](boost::system::error_code ec, std::size_t /*bytes_sent*/) {
                if (ec) {
                    SPDLOG_ERROR("UDP 发送错误: {}", ec.message());
                } else {
                    sent_count++;
                }
            }
        )
    );
}

void UdpClient::sendMessageTo(const std::string& message, const std::string& ip, uint16_t port) {
    try {
        udp::endpoint ep(boost::asio::ip::make_address(ip), port);
        sendMessageTo(message, ep);
    } catch (const std::exception& e) {
        SPDLOG_ERROR("目标地址无效 {}:{} - {}", ip, port, e.what());
    }
}

void UdpClient::setMessageCallback(MessageCallback cb) {
    message_cb = std::move(cb);
}