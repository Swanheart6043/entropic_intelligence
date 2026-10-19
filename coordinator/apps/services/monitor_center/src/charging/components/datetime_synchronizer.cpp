#include <ctime>
#include <string>
#include <string_view>
#include <chrono>
#include <stdexcept>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "datetime_synchronizer.hpp"

TimeSynchronizer::TimeSynchronizer() {
    int timeout_ms = 1000;
    socket_from_control.setsockopt(ZMQ_SUBSCRIBE, "charging_info", strlen("charging_info"));
    socket_from_control.setsockopt(ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    socket_from_control.connect("tcp://127.0.0.1:6060");

    // 感知中心 REQ socket 的配置与重建共用同一份逻辑
    rebuild_perceiver_socket();
}

TimeSynchronizer::~TimeSynchronizer() {
    stop();
}

void TimeSynchronizer::start() {
    if (running) {
        SPDLOG_WARN("校时线程，已经在启动了");
        return;
    }
    running = true;
    worker_thread = std::thread(&TimeSynchronizer::time_synchronous_loop, this);
}

void TimeSynchronizer::stop() {
    if (!running) {
        SPDLOG_WARN("校时线程，没有启动，无需停止");
        return;
    }
    running = false;
    if (worker_thread.joinable()) worker_thread.join();
}

void TimeSynchronizer::time_synchronous_loop() {
    using namespace nlohmann;
    using namespace std::chrono;

    bool boot_synced = false;
    steady_clock::time_point last_boot_attempt{};  // 零值,首轮立即触发
    steady_clock::time_point last_sync{};

    while (running) {
        try {
            // 开机首次校时:不依赖充电状态,失败按 kBootRetryInterval 重试,成功一次后不再走这条路径
            if (!boot_synced && steady_clock::now() - last_boot_attempt >= kBootRetryInterval) {
                last_boot_attempt = steady_clock::now();
                SPDLOG_INFO("开机校时尝试");
                if (do_sync()) {
                    boot_synced = true;
                    last_sync = steady_clock::now();
                }
            }

            zmq::message_t topic_msg, zmq_message;
            if (!socket_from_control.recv(topic_msg)) continue;
            if (!socket_from_control.recv(zmq_message)) continue;

            std::string message(static_cast<char*>(zmq_message.data()), zmq_message.size());
            bool is_charge = json::parse(message).at("is_auto_charge").get<bool>();
            if (!is_charge) continue;

            // 充电中,但距上次校时不足 kChargeSyncInterval,跳过
            if (steady_clock::now() - last_sync < kChargeSyncInterval) continue;

            if (do_sync()) last_sync = steady_clock::now();
        } catch (const std::exception& e) {
            SPDLOG_ERROR("校时线程异常: {}", e.what());
        }
    }
}

bool TimeSynchronizer::do_sync() {
    std::string error;
    const auto timestamp = cloud_gateway_.get_server_time(&error);
    if (!timestamp) {
        SPDLOG_WARN("通过web_gateway获取服务器时间失败: {}", error);
        return false;
    }
    const int64_t date_time = *timestamp; // 毫秒级13位时间戳

    sync_oneself(date_time);
    sync_perceiver(date_time);
    sync_high_definition_camera(date_time);
    sync_infrared_camera(date_time);
    sync_front_camera(date_time);
    sync_back_camera(date_time);
    sync_left_camera(date_time);
    sync_right_camera(date_time);
    return true;
}

void TimeSynchronizer::rebuild_perceiver_socket() {
    socket_to_perceiver.close();
    socket_to_perceiver = zmq::socket_t(ctx, zmq::socket_type::req);
    int timeout = kZmqTimeoutMs;
    int linger = kLingerMs;
    int sndhwm = kSndHwm;
    socket_to_perceiver.setsockopt(ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    socket_to_perceiver.setsockopt(ZMQ_LINGER, &linger, sizeof(linger));
    socket_to_perceiver.setsockopt(ZMQ_SNDHWM, &sndhwm, sizeof(sndhwm));
    socket_to_perceiver.connect(kPerceiverEndpoint);
}

void TimeSynchronizer::sync_oneself(int64_t date_time) {
    // 主机自己的时钟:偏差小于阈值就不动,避免无谓的 CLOCK_REALTIME 跳变
    int64_t local_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t delta_ms = date_time - local_ms;
    if (std::abs(delta_ms) < kSyncThreshold.count()) {
        SPDLOG_INFO("本地时间与服务器偏差 {}ms,无需校时", delta_ms);
        return;
    }

    struct timespec ts;
    ts.tv_sec = date_time / 1000;
    ts.tv_nsec = (date_time % 1000) * 1000000;
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
        // EPERM = 1: 权限不足 EINVAL = 22: 时间值无效
        SPDLOG_ERROR("校时失败: {} ({})", strerror(errno), errno);
        return;
    }
    SPDLOG_INFO("系统校时成功,偏差 {}ms", delta_ms);
}

void TimeSynchronizer::sync_perceiver(int64_t date_time) {
    nlohmann::json payload = {{"cmd", "date_time_set"}, {"date", date_time}};

    // REQ 状态机:send 失败后 socket 停在"待发送"态,直接 recv 会抛 EFSM,必须重建
    auto send_result = socket_to_perceiver.send(zmq::buffer(payload.dump()), zmq::send_flags::none);
    if (!send_result) {
        SPDLOG_WARN("通知感知中心校时发送失败,重建socket: {}", payload.dump());
        rebuild_perceiver_socket();
        return;
    }

    zmq::message_t reply;
    if (!socket_to_perceiver.recv(reply, zmq::recv_flags::none)) {
        SPDLOG_WARN("感知中心校时回复超时,重建socket");
        rebuild_perceiver_socket();
        return;
    }
    SPDLOG_INFO("感知中心校时确认: {}", reply.to_string_view());
}

void TimeSynchronizer::sync_high_definition_camera(int64_t date_time) {
    try {
        hik::HikvisionIsapiClient cam{"10.10.10.40", 80, "admin", "heyn_888", 1};

        // 毫秒级转秒级
        std::time_t seconds = static_cast<std::time_t>(date_time / 1000);

        // 转北京时间：UTC + 8
        seconds += 8 * 60 * 60;

        std::tm tm{};
        gmtime_r(&seconds, &tm);

        char time_buf[32]{};
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S+08:00", &tm);

        const std::string body =
            R"(<?xml version="1.0" encoding="UTF-8"?>)"
            R"(<Time version="2.0" xmlns="http://www.isapi.org/ver20/XMLSchema">)"
            R"(<timeMode>manual</timeMode>)"
            "<localTime>" + std::string(time_buf) + "</localTime>"
            R"(<timeZone>CST-8:00:00</timeZone>)"
            R"(</Time>)";
        cam.raw_put("/ISAPI/System/time", body);

        SPDLOG_INFO("高清摄像头校时成功: {}", time_buf);
    } catch (const std::exception& e) {
        SPDLOG_INFO("高清摄像头校时异常: {}", e.what());
    }
}

void TimeSynchronizer::sync_infrared_camera(int64_t date_time) {
    try {
        std::time_t seconds = static_cast<std::time_t>(date_time / 1000);

        // 不依赖 Ubuntu 系统时区，手动转北京时间
        seconds += 8 * 60 * 60;
        std::tm tm{};
        gmtime_r(&seconds, &tm);
        char time_buf[32]{};
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d%%20%H:%M:%S", &tm);

        httplib::Client cli("10.10.10.41", 80);
        cli.set_digest_auth("admin", "heyn_888");
        cli.set_connection_timeout(std::chrono::seconds(3));
        cli.set_read_timeout(std::chrono::seconds(5));

        const std::string path = "/cgi-bin/global.cgi?action=setCurrentTime&time=" + std::string(time_buf);
        auto res = cli.Get(path.c_str());
        if (!res) {
            SPDLOG_INFO("Dahua setCurrentTime connection failed");
            return;
        }
        if (res->status < 200 || res->status >= 300) {
            SPDLOG_INFO("Dahua setCurrentTime HTTP failed, status=" + std::to_string(res->status) + ", body=" + res->body);
            return;
        }

        // 大华成功通常返回 OK
        SPDLOG_INFO("红外摄像头校时成功: {}", time_buf);
    } catch (const std::exception& e) {
        SPDLOG_INFO("红外摄像头校时异常: {}", e.what());
    }
}

void TimeSynchronizer::sync_front_camera(int64_t date_time) {
    sync_merlin_camera("前摄像头", "10.10.10.42", date_time);
}

void TimeSynchronizer::sync_back_camera(int64_t date_time) {
    sync_merlin_camera("后摄像头", "10.10.10.43", date_time);
}

void TimeSynchronizer::sync_left_camera(int64_t date_time) {
    sync_merlin_camera("左摄像头", "10.10.10.44", date_time);
}

void TimeSynchronizer::sync_right_camera(int64_t date_time) {
    sync_merlin_camera("右摄像头", "10.10.10.45", date_time);
}

void TimeSynchronizer::sync_merlin_camera(std::string_view camera_name, std::string_view host, int64_t date_time) {
    try {
        std::time_t seconds = static_cast<std::time_t>(date_time / 1000);
        seconds += 8 * 60 * 60; // UTC -> 北京时间

        std::tm tm{};
        if (gmtime_r(&seconds, &tm) == nullptr) {
            throw std::runtime_error("gmtime_r failed");
        }

        const nlohmann::json payload = {
            {"TimeCfg", {
                {"year", tm.tm_year + 1900},
                {"month", tm.tm_mon + 1},
                {"day", tm.tm_mday},
                {"hour", tm.tm_hour},
                {"minute", tm.tm_min},
                {"second", tm.tm_sec}
            }}
        };
        httplib::Client cli(std::string(host), 80);
        cli.set_basic_auth("admin", "heyn_888");
        cli.set_connection_timeout(std::chrono::seconds(3));
        cli.set_read_timeout(std::chrono::seconds(5));
        cli.set_write_timeout(std::chrono::seconds(5));

        auto res = cli.Post("/merlin/SetTimeCfg.cgi", payload.dump(), "application/json");
        if (!res) {
            SPDLOG_ERROR("{}校时连接失败: {}", camera_name, httplib::to_string(res.error()));
            return;
        }
        if (res->status != 200) {
            SPDLOG_ERROR("{}校时 HTTP 失败, status={}, body={}", camera_name, res->status, res->body);
            return;
        }

        const auto response = nlohmann::json::parse(res->body);
        const auto& result = response.at("result");
        if (result.value("message", std::string{}) != "OK" ||
            result.value("num", 0) != 200) {
            SPDLOG_ERROR("{}校时接口返回失败: {}", camera_name, res->body);
            return;
        }
        SPDLOG_INFO(
            "{}校时成功: {:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
            camera_name,
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec
        );
    } catch (const std::exception& e) {
        SPDLOG_ERROR("{}校时异常: {}", camera_name, e.what());
    }
}
