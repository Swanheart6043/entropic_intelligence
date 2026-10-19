#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include <spdlog/spdlog.h>
#include "httplib.hpp"
#include "config_parser.hpp"
#include "db_connection.hpp"
#include "mcu_controller.hpp"

McuController::McuController() {
    zmq_socket_web.setsockopt(ZMQ_SNDTIMEO, 1000);
    zmq_socket_power.setsockopt(ZMQ_SNDTIMEO, 1000);
	zmq_socket_web.connect("tcp://127.0.0.1:2007");
    zmq_socket_power.connect("tcp://127.0.0.1:6061");
}

void McuController::release_emergency_stop() {
    try {
		zmq::context_t ctx{1};
        zmq::socket_t push{ctx, zmq::socket_type::push};
        push.connect("tcp://127.0.0.1:6061");

        nlohmann::json message = {{"type", "release_emergency_stop"}};
        push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);
		SPDLOG_INFO("通知主控解除急停: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("通知主控解除急停异常: {}", e.what());
	}
}

void McuController::cancel_charging() {
	try {
		using namespace nlohmann;

		zmq::context_t ctx{1};
        zmq::socket_t push{ctx, zmq::socket_type::push};
        push.connect("tcp://127.0.0.1:6061");
        json message = {{"type", "cancel_charging"}};
		push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

		SPDLOG_INFO("通知主控取消充电: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("通知主控取消充电异常: {}", e.what());
	}
}

void McuController::reboot_power() {
	try {
		using namespace nlohmann;

		zmq::context_t ctx{1};
        zmq::socket_t push{ctx, zmq::socket_type::push};
        push.connect("tcp://127.0.0.1:6061");
		json message = {{"type", "reboot"}};
        push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

		SPDLOG_INFO("通知主控重启: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("通知主控重启异常: {}", e.what());
	}
}

void McuController::shutdown() {
    try {
		using namespace nlohmann;

		zmq::context_t ctx{1};
        zmq::socket_t push{ctx, zmq::socket_type::push};
        push.connect("tcp://127.0.0.1:6061");
		json message = {{"type", "shutdown"}};
        push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

		SPDLOG_INFO("通知主控关机: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("通知主控关机异常: {}", e.what());
	}
}

void McuController::turn_on_head_light() {
	using namespace nlohmann;

	zmq::context_t ctx{1};
	zmq::socket_t push{ctx, zmq::socket_type::push};
	int send_timeout = 1000;
	push.setsockopt(ZMQ_SNDTIMEO, send_timeout);
	push.connect("tcp://127.0.0.1:6061");
	json message = {
		{"type", "lighting_control"},
		{"params", {{"type", "head_light"}, {"is_on", true}}}
	};
	push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

	json ack = {
		{"Header", {
			{"CmdType", "MCUStatusPost"}, 
			{"Sn", config->get_sn()}, 
			{"Time", std::time(nullptr)}
		}},
        {"MCUStatus", {{"HeadLightStatus", 1}}}
	};
	zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
	zmq_socket_web.connect("tcp://127.0.0.1:2007");
	zmq_socket_web.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

	try {
		sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE settings_basic SET config_value = 'on' WHERE config_key = 'head_light'";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        stmt->executeUpdate(sql);

        SPDLOG_INFO("大灯已开启，数据库已更新");
	} catch (const std::exception& e) {
		SPDLOG_ERROR("开大灯异常: {}", e.what());
	}
}

void McuController::turn_off_head_light() {
	using namespace nlohmann;

	zmq::context_t ctx{1};
	zmq::socket_t push{ctx, zmq::socket_type::push};
	int send_timeout = 1000;  // 1s 超时
	push.setsockopt(ZMQ_SNDTIMEO, send_timeout);
	push.connect("tcp://127.0.0.1:6061");
	json message = {
		{"type", "lighting_control"},
		{"params", {{"type", "head_light"}, {"is_on", false}}}
	};
	push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

	json ack = {
		{"Header", {
			{"CmdType", "MCUStatusPost"}, 
			{"Sn", config->get_sn()}, 
			{"Time", std::time(nullptr)}
		}},
        {"MCUStatus", {{"HeadLightStatus", 0}}}
	};
	zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
	zmq_socket_web.connect("tcp://127.0.0.1:2007");
	zmq_socket_web.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

	try {
		sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE settings_basic SET config_value = 'off' WHERE config_key = 'head_light'";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        stmt->executeUpdate(sql);

		SPDLOG_INFO("关大灯: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("关大灯异常: {}", e.what());
	}
}

void McuController::turn_on_warning_light() {
	using namespace nlohmann;

	zmq::context_t ctx{1};
	zmq::socket_t push{ctx, zmq::socket_type::push};
	int send_timeout = 1000;  // 100ms 超时
	push.setsockopt(ZMQ_SNDTIMEO, send_timeout);
	push.connect("tcp://127.0.0.1:6061");
	SPDLOG_INFO("开警灯:");
	json message = {
		{"type", "lighting_control"},
		{"params", {{"type", "warning_light"}, {"is_on", true}}}
	};
	push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

	json ack = {
		{"Header", {
			{"CmdType", "MCUStatusPost"}, 
			{"Sn", config->get_sn()}, 
			{"Time", std::time(nullptr)}
		}},
        {"MCUStatus", {{"AlarmLightStatus", 1}}}
	};
	zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
	zmq_socket_web.connect("tcp://127.0.0.1:2007");
	zmq_socket_web.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

	try {
		sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE settings_basic SET config_value = 'on' WHERE config_key = 'warning_light'";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        stmt->executeUpdate(sql);

		SPDLOG_INFO("开警灯: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("开警灯异常: {}", e.what());
	}
}

void McuController::turn_off_warning_light() {
	using namespace nlohmann;

	zmq::context_t ctx{1};
	zmq::socket_t push{ctx, zmq::socket_type::push};
	int send_timeout = 1000;  // 100ms 超时
	push.setsockopt(ZMQ_SNDTIMEO, send_timeout);
	push.connect("tcp://127.0.0.1:6061");
	json message = {
		{"type", "lighting_control"},
		{"params", {{"type", "warning_light"}, {"is_on", false}}}
	};
	push.send(zmq::buffer(message.dump()), zmq::send_flags::dontwait);

	json ack = {
		{"Header", {
			{"CmdType", "MCUStatusPost"}, 
			{"Sn", config->get_sn()}, 
			{"Time", std::time(nullptr)}
		}},
        {"MCUStatus", {{"AlarmLightStatus", 0}}}
	};
	zmq::socket_t zmq_socket_web{ctx, zmq::socket_type::push};
	zmq_socket_web.connect("tcp://127.0.0.1:2007");
	zmq_socket_web.send(zmq::buffer(ack.dump()), zmq::send_flags::dontwait);

	try {
		sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE settings_basic SET config_value = 'off' WHERE config_key = 'warning_light'";
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        stmt->executeUpdate(sql);

		SPDLOG_INFO("关警灯: {}", message.dump());
	} catch (const std::exception& e) {
		SPDLOG_ERROR("关警灯异常: {}", e.what());
	}
}

void McuController::reboot_industrial_computer() {
    try {
		SPDLOG_INFO("准备重启");
		std::system("sudo reboot");
	} catch (const std::exception& e) {
		SPDLOG_ERROR("重启异常: {}", e.what());
	}
}

void McuController::reboot_navigation() {
	try {
		SPDLOG_INFO("重启导航成功");
		auto http = std::make_unique<httplib::Client>("http://10.10.10.10:8080");
		http->Get("/gs-robot/cmd/reboot");
	} catch (const std::exception& e) {
		SPDLOG_ERROR("重启导航异常: {}", e.what());
	}
}