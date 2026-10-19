#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include "video_handler.hpp"

AudioController::AudioController() {
    socket_streaming.connect("ipc://@audio_video_pull");
    socket_recording_center_files_query.connect("ipc://@recording_center_files_query");
    socket_recording_center_file_streaming.connect("ipc://@recording_center_file_streaming");

    zmq_socket_publisher.bind("tcp://*:4283");
}

void AudioController::handle_video_file_list(WebSocketClient& websocket_connect, const std::string& message) {
    using namespace nlohmann;
    
    json message_json = json::parse(message);
    std::string channel = message_json.at("ChannelNum").get<std::string>();
    std::string start_time = message_json.at("QueryBeginTime").get<std::string>();
    std::string end_time = message_json.at("QueryEndTime").get<std::string>();

    json params = {
        {"type", "recording-query"},
        {"channel", ""},
        {"start_time", start_time},
        {"end_time", end_time},
    };
    if (channel == "1") {
        params.at("channel") = "front";
    }
    if (channel == "2") {
        params.at("channel") = "high_definition";
    }
    if (channel == "3") {
        params.at("channel") = "infrared";
    }
    if (channel == "4") {
        params.at("channel") = "rear";
    }
    if (channel == "5") {
        params.at("channel") = "left";
    }
    if (channel == "6") {
        params.at("channel") = "right";
    }
    socket_recording_center_files_query.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
	SPDLOG_INFO("通知音视频中心: {}", params.dump());
}

void AudioController::handle_video_file_streaming(WebSocketClient& websocket_connect, const std::string& message) {
    using namespace nlohmann;
    
    json message_json = json::parse(message);
    json request = message_json.at("Request").get<json>();
    std::string channel = request.at("Channel").get<std::string>();
    std::string file_name = request.at("FileName").get<std::string>();
    std::string start_time = request.at("StartTime").get<std::string>();
    std::string end_time = request.at("EndTime").get<std::string>();

    json ack = {
        {"Header", {
            {"CmdType", "PTZ_RecordVideo_Url_Get"}, 
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    websocket_connect.send_message(ack.dump());
    SPDLOG_INFO("发送平台回复: {}", ack.dump());

    json params = {
        {"type", "recording-streaming"},
        {"channel", ""},
        {"file_name", file_name}
    };
    if (channel == "1") {
        params.at("channel") = "front";
    }
    if (channel == "2") {
        params.at("channel") = "high_definition";
    }
    if (channel == "3") {
        params.at("channel") = "infrared";
    }
    if (channel == "4") {
        params.at("channel") = "rear";
    }
    if (channel == "5") {
        params.at("channel") = "left";
    }
    if (channel == "6") {
        params.at("channel") = "right";
    }
    socket_recording_center_file_streaming.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
	SPDLOG_INFO("通知音视频中心: {}", params.dump());
}

void AudioController::handle_video_file_control(WebSocketClient& websocket_connect, const std::string& message) {
    using namespace nlohmann;
    
    json message_json = json::parse(message);
    json request = message_json.at("Request").get<json>();
    std::string channel = request.at("Channel").get<std::string>();
    std::string file_name = request.at("FileName").get<std::string>();
    std::string start_time = request.at("StartTime").get<std::string>();
    std::string end_time = request.at("EndTime").get<std::string>();

    json ack = {
        {"Header", {
            {"CmdType", "PTZ_RecordVideo_Control"}, 
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    websocket_connect.send_message(ack.dump());
    SPDLOG_INFO("发送平台回复: {}", ack.dump());

    json params = {
        {"type", "recording-streaming"},
        {"channel", ""},
        {"file_name", file_name}
    };
    if (channel == "1") {
        params.at("channel") = "front";
    }
    if (channel == "2") {
        params.at("channel") = "high_definition";
    }
    if (channel == "3") {
        params.at("channel") = "infrared";
    }
    if (channel == "4") {
        params.at("channel") = "rear";
    }
    if (channel == "5") {
        params.at("channel") = "left";
    }
    if (channel == "6") {
        params.at("channel") = "right";
    }
    socket_recording_center_file_streaming.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
	SPDLOG_INFO("通知音视频中心: {}", params.dump());
}

void AudioController::notification_streaming(WebSocketClient& websocket_connect, const std::string& message) {
	try {
		using namespace nlohmann;

		json message_json = json::parse(message);
		json request = message_json.at("Request");
		bool enable_or_disable = request.at("Option").get<std::string>() == "1";
		std::string channel = request.at("Channel").get<std::string>();

        json ack = {
            {"Header", {
                {"CmdType", "PTZ_Video_Url_Get"}, 
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"Result", 1}, {"Data", ""}
        };
        websocket_connect.send_message(ack.dump());
        SPDLOG_INFO("发送平台回复: {}", ack.dump());
		
		json params = {
			{"type", enable_or_disable ? "enable-streaming" : "disable-streaming"},
			{"channel", ""},
		};
		if (channel == "1") {
			params.at("channel") = "front";
		}
		if (channel == "2") {
			params.at("channel") = "high_definition";
		}
		if (channel == "3") {
			params.at("channel") = "infrared";
		}
		if (channel == "4") {
			params.at("channel") = "rear";
		}
		if (channel == "5") {
			params.at("channel") = "left";
		}
		if (channel == "6") {
			params.at("channel") = "right";
		}
		socket_streaming.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);
		SPDLOG_INFO("通知音视频层播放: {}", params.dump());

	} catch(const std::exception& e) {
		SPDLOG_ERROR("通知音视频层异常: {}", e.what());
	}
}

void AudioController::handle_skeleton_streaming(WebSocketClient& websocket_connect, const std::string& message) {
    nlohmann::json payload = {{"command", "forward"}, {"content", message}};
    zmq_socket_publisher.send(zmq::buffer(payload.dump()), zmq::send_flags::dontwait);

    nlohmann::json ack = {
        {"Header", {
            {"CmdType", "PTZ_AlgorithmVideo_Url_Get"}, 
            {"Sn", config->get_sn()},
            {"Time", std::time(nullptr)}
        }},
        {"Result", 1}, {"Data", ""}
    };
    websocket_connect.send_message(ack.dump());
}
