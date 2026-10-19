#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "edge_http_server.hpp"
#include "routes/user_routes.hpp"
#include "routes/device_routes.hpp"

EdgeHttpServer::EdgeHttpServer(nlohmann::json& robot_info, std::mutex& robot_info_mtx)
    : robot_info_(robot_info), robot_info_mtx_(robot_info_mtx) {}

EdgeHttpServer::~EdgeHttpServer() {
    stop();
}

void EdgeHttpServer::start() {
    if (running) {
        SPDLOG_WARN("边缘http服务线程, 已经在运行了");
        return;
    }
    running = true;
    worker_thread = std::thread(&EdgeHttpServer::http_loop, this);
}

void EdgeHttpServer::stop() {
    if (!running) return;
    running = false;
    server_.stop();   // 立即关闭 listen socket，listen() 随即返回
    if (worker_thread.joinable()) worker_thread.join();
    SPDLOG_INFO("边缘http服务已停止");
}

void EdgeHttpServer::http_loop() {
    register_user_routes(server_, robot_info_, robot_info_mtx_);
    register_device_routes(server_, robot_info_, robot_info_mtx_);

    server_.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json error = {
            {"error", "Not Found"},
            {"path", req.path},
            {"method", req.method}
        };
        res.set_content(error.dump(), "application/json");
        // 注意：set_error_handler 只在 status>=400 时触发，404 也算
    });

    // 可选：缩短 keep-alive 超时，让连接更快释放
    server_.set_keep_alive_timeout(5);

    SPDLOG_INFO("边缘http服务启动 0.0.0.0:8088");
    if (!server_.listen("0.0.0.0", 8088)) {  // 阻塞，stop() 后返回
        SPDLOG_ERROR("http listen 失败");
    }
}