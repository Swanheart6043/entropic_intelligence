#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "face_database_handler.hpp"

FaceDatabaseHandler::FaceDatabaseHandler() {
    int timeout = 1000;
    int linger = 1000;
    int sndhwm = 100;
    socket.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket.connect("tcp://10.10.10.30:2430");
}

void FaceDatabaseHandler::handle(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        using namespace nlohmann;
    
        json message_json = json::parse(message);
        json params = {
            {"cmd", "face_sync"},
            {"sn", config_->get_sn()},
            {"faces", message_json.at("GroupList")},
        };
        SPDLOG_INFO("通知RDK: {}", params.dump());

        auto result = socket.send(zmq::buffer(params.dump()), zmq::send_flags::none);
        if (!result) {
            SPDLOG_ERROR("人脸服务未响应, 参数: {}", message);
            return;
        }
        
        json ack = {
            {"Header", {
                {"CmdType", "RC_Robot_Register_Face_Update_Notice"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", "1"}, {"Data", ""}
        };
        SPDLOG_INFO("应答人脸库下发: {}", ack.dump());
        websocket_connect.send_message(ack.dump());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("人脸库下发异常: {}", e.what());
    }
}

void FaceDatabaseHandler::answer(WebSocketClient& websocket_connect, const std::string& message) {
    try {
        nlohmann::json ack = {
            {"Header", {
                {"CmdType", "RC_FaceRecognized_Broadcast_Set"},
                {"Sn", config_->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", "1"}, {"Data", ""}
        };
        SPDLOG_INFO("应答人脸配置: {}", ack.dump());
        websocket_connect.send_message(ack.dump());
    } catch (const std::exception& e) {
        SPDLOG_ERROR("人脸配置下发异常: {}", e.what());
    }
}