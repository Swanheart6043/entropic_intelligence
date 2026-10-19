#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "core_thread.hpp"

static std::atomic<int> err_count{0};

CommunicationService::CommunicationService(std::shared_ptr<boost::asio::io_context> io_ctx) : udp(io_ctx) {	
	pull.bind("tcp://*:6061");

	udp.init("10.10.10.20", 6001, 6001);
	udp.setMessageCallback([this](const std::string& message, const udp::endpoint& ep) {receive_message_loop(message, ep);});
}

CommunicationService::~CommunicationService() {
	stop();
}

void CommunicationService::start() {
	if (running) {
        SPDLOG_WARN("通信线程已经在运行了！");
        return;
    }
	running = true;

	worker_thread = std::thread(&CommunicationService::forward_message_loop, this);
	udp.start();
}

void CommunicationService::stop() {
	if (!running) {
		SPDLOG_WARN("通信线程没在运行，无需停止");
		return;
	}
    running = false;

	if (worker_thread.joinable()) worker_thread.join();
    udp.stop();
}

// 监听接收消息
void CommunicationService::receive_message_loop(const std::string& message, const udp::endpoint& ep) {
	const uint8_t* buf = reinterpret_cast<const uint8_t*>(message.data());
	uint16_t funid = 0;
	std::vector<uint8_t> data;
	std::string err;

	if (!unpacking(buf, message.size(), funid, data, err)) {
		if (++err_count % 100 == 1) {
			// 每100次打印一次
        	SPDLOG_WARN("解包失败(累计 {} 次) 来源={}: {}", err_count.load(), ep.address().to_string(), err);
    	}
    	return;
	}
	if (data.empty()) return;
	match_receive_message(funid, data);
}

// 匹配接收消息
void CommunicationService::match_receive_message(uint16_t funid, std::vector<uint8_t>& data) {
    switch (funid) {
        case 0x1110: {
            parser.parse_battery_info(data); // 电池
            break;
        }
        case 0x1202: {
            parser.parse_charging_status(data); // 充电连接状态
            break;
        }
        case 0x1201: {
            parser.parse_system_status(data); // 系统状态
            break;
        }
        case 0x110C: {
            parser.parse_temperature_humidity(data); // 温湿度
            break;
        }
        case 0x1209: {
            parser.parse_total_runtime(data); // 运行周期
            break;
        }
        case 0x1204: {
            parser.parse_button_status(data); // 按钮
            break;
        }
        case 0x1104: {
            parser.parse_version(data); // 电源板版本
            break;
        }
        case 0x1101: {
            parser.parse_gps(data); // GPS
            break;
        }
        default: {
            break; // 未知 funid 忽略
        }
    }
}

// 监听转发消息
void CommunicationService::forward_message_loop() {
    while (running) {
        try {
            zmq::message_t zmq_message;
            auto has_message = pull.recv(zmq_message, zmq::recv_flags::none);
            if (!has_message) continue;
            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            SPDLOG_INFO("监听到命令消息: {}", message);
            match_forward_message(message);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("IPC 命令监听线程异常: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

// 匹配转发消息
void CommunicationService::match_forward_message(const std::string& message) {
    nlohmann::json message_json = nlohmann::json::parse(message);
    std::string type = message_json.value("type", "");
    // 大灯 / 警灯
    if (type == "lighting_control") {
        auto params = message_json.at("params");
        std::string light_type = params.value("type", "");
		bool is_on = params.at("is_on").get<bool>();

        if (light_type == "head_light" && is_on) {
			send_frame(0x1407, { 1, 0x01, 10 });
			return;
        }
		if (light_type == "head_light" && !is_on) {
			send_frame(0x1407, { 1, 0x00 });
			return;
		}
        if (light_type == "warning_light" && is_on) {
            send_frame(0x1407, { 2, 0x01, 10 });
			send_frame(0x1407, { 3, 0x01, 10 });
			return;
        }
		if (light_type == "warning_light" && !is_on) {
			send_frame(0x1407, { 2, 0x00 });
			send_frame(0x1407, { 3, 0x00 });
			return;
		}
    }
    // 对桩充电
    if (type == "charging") { 
        send_frame(0x1405, { 0x01 });
        return; 
    }
    // 取消充电
    if (type == "cancel_charging") { 
        send_frame(0x1405, { 0x00 });
        return;
    }
    // 解除急停
    if (type == "release_emergency_stop") { 
        send_frame(0x1515, { 0x01 });
        return;
    }
    // 重启
    if (type == "reboot") { 
        send_frame(0x1480, {});
        return;
    }
    // 关机
    if (type == "shutdown") {
        send_frame(0x1486, {});
        return;
    }
}

// 辅助方法: 解帧
bool CommunicationService::unpacking(const uint8_t* buf, size_t bytes_recvd, uint16_t& out_funid, std::vector<uint8_t>& out_data, std::string& errReason) {
    errReason.clear();
    out_data.clear();

    if (bytes_recvd < 8) {
        errReason = "too short (<8)";
        return false;
    }
    if (buf[0] != 0x68 || buf[1] != 0x48) {
        errReason = "invalid header (FrameComp/ident)";
        return false;
    }

    uint16_t len_field = static_cast<uint16_t>(buf[2]) | (static_cast<uint16_t>(buf[3]) << 8);
    if (len_field < 8) {
        errReason = "len field < 8";
        return false;
    }
    // UDP 保留报文边界，bytes_recvd 应等于 len_field
    if (len_field != bytes_recvd) {
        errReason = "len field mismatch with received size (len_field=" + std::to_string(len_field) + " recv=" + std::to_string(bytes_recvd) + ")";
        return false;
    }

    // CRC 范围：FrameComp 到 data 最后一字节 => 前 (len_field - 2) 字节
    uint16_t calc_crc = CRC16_table_lookup(buf, len_field - 2);
    uint16_t recv_crc = static_cast<uint16_t>(buf[len_field - 2]) | (static_cast<uint16_t>(buf[len_field - 1]) << 8);
    if (calc_crc != recv_crc) {
        errReason = fmt::format("crc mismatch: calc=0x{:04X} recv=0x{:04X}", calc_crc, recv_crc);
        return false;
    }

    // funid（小端）
    out_funid = static_cast<uint16_t>(buf[4]) | (static_cast<uint16_t>(buf[5]) << 8);

    // data 长度 = len_field - 8 (header 2 + len 2 + funid 2 + crc 2)
    size_t data_len = static_cast<size_t>(len_field) - 8;
    if (data_len > 0) {
        out_data.insert(out_data.end(), buf + 6, buf + 6 + data_len);
    }
    return true;
}

// 辅助方法: 发帧
void CommunicationService::send_frame(uint16_t funid, const std::vector<uint8_t>& data) {
	std::vector<uint8_t> packet;
	packet.reserve(8 + data.size());

	// 帧头
	packet.push_back(0x68);
	packet.push_back(0x48);
	
	// 总长 = 8 + data 长度
	const uint16_t total_len = static_cast<uint16_t>(8 + data.size());
	append_packet(packet, total_len);
	
	// funid
	append_packet(packet, funid);
	
	// data
	if (!data.empty()) packet.insert(packet.end(), data.begin(), data.end());
	
	// 加CRC, 不含CRC自身
	const unsigned short crc = CRC16_table_lookup(reinterpret_cast<const unsigned char*>(packet.data()), static_cast<unsigned int>(packet.size()));
	append_packet(packet, crc);
	
	// udp发送
	udp.sendMessageTo(std::string(reinterpret_cast<const char*>(packet.data()), packet.size()), "10.10.10.20", 6001);
}

// 辅助方法: 追加位
void CommunicationService::append_packet(std::vector<uint8_t>& v, uint16_t x) {
	v.push_back(static_cast<uint8_t>(x & 0xFF));
	v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}