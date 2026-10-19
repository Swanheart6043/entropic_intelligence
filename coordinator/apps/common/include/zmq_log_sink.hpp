/*
 * ZMQ日志Sink - 发送日志到运维系统
 * 
 * 使用方法：
 * 1. 在你的C++项目中包含此头文件
 * 2. 在初始化日志时添加此sink
 * 
 * 示例：
 *   auto zmq_sink = std::make_shared<ZmqLogSink>("task_center");
 *   logger->sinks().push_back(zmq_sink);
 */
#ifndef ZMQ_LOG_SINK_HPP
#define ZMQ_LOG_SINK_HPP
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>

namespace patrol_robot {
template<typename Mutex>
class ZmqLogSinkImpl : public spdlog::sinks::base_sink<Mutex> {
public:
    /**
     * 构造函数
     * 
     * @param process_name 进程名称（用于标识日志来源）
     * @param endpoint ZMQ连接地址，默认tcp://localhost:5556
     */
    explicit ZmqLogSinkImpl(
        const std::string& process_name,
        const std::string& endpoint = "tcp://localhost:5556")
        : process_name_(process_name)
        , context_(1)
        , socket_(context_, zmq::socket_type::pub)
    {
        try {
            socket_.connect(endpoint);
            // 等待连接建立
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const zmq::error_t& e) {
            // 连接失败不影响主程序运行
            std::cerr << "ZMQ sink connection failed: " << e.what() << std::endl;
        }
    }

    ~ZmqLogSinkImpl() {
        try {
            socket_.close();
            context_.close();
        } catch (...) {
            // 忽略析构时的异常
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        try {
            // 构建JSON格式的日志
            nlohmann::json log_json;
            
            // 时间戳（ISO 8601格式）
            auto time_point = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(time_point);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()) % 1000;
            
            std::tm tm_buf;
            localtime_r(&time_t, &tm_buf);
            
            char time_buffer[32];
            std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%dT%H:%M:%S", &tm_buf);
            std::string timestamp = std::string(time_buffer) + "." + std::to_string(ms.count());
            
            log_json["timestamp"] = timestamp;
            log_json["process"] = process_name_;
            log_json["thread"] = msg.thread_id;
            log_json["level"] = level_to_string(msg.level);
            log_json["message"] = std::string(msg.payload.data(), msg.payload.size());
            
            // 可选：添加源码位置信息
            if (msg.source.filename) {
                log_json["file"] = msg.source.filename;
                log_json["line"] = msg.source.line;
                log_json["function"] = msg.source.funcname;
            }
            
            // 发送到ZMQ（非阻塞）
            std::string json_str = log_json.dump();
            zmq::message_t zmq_msg(json_str.data(), json_str.size());
            socket_.send(zmq_msg, zmq::send_flags::dontwait);
            
        } catch (const std::exception& e) {
            // 发送失败不影响主程序运行
            // 可以选择性地输出到stderr
            // std::cerr << "ZMQ sink error: " << e.what() << std::endl;
        }
    }

    void flush_() override {
        // ZMQ不需要显式flush
    }

private:
    std::string level_to_string(spdlog::level::level_enum level) {
        switch (level) {
            case spdlog::level::trace: return "trace";
            case spdlog::level::debug: return "debug";
            case spdlog::level::info: return "info";
            case spdlog::level::warn: return "warn";
            case spdlog::level::err: return "error";
            case spdlog::level::critical: return "critical";
            default: return "unknown";
        }
    }

    std::string process_name_;
    zmq::context_t context_;
    zmq::socket_t socket_;
};

// 类型定义
using ZmqLogSink = ZmqLogSinkImpl<std::mutex>;
using ZmqLogSink_mt = ZmqLogSinkImpl<std::mutex>;
using ZmqLogSink_st = ZmqLogSinkImpl<spdlog::details::null_mutex>;

} // namespace patrol_robot

#endif // ZMQ_LOG_SINK_HPP


/*
 * 使用示例：
 * 
 * // 在你的日志初始化代码中：
 * 
 * #include "zmq_log_sink.hpp"
 * 
 * void setup_logger() {
 *     // 创建logger
 *     auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
 *     auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
 *         "logs/task_center.log", 1024 * 1024 * 10, 3);
 *     
 *     // 添加ZMQ sink（发送到运维系统）
 *     auto zmq_sink = std::make_shared<patrol_robot::ZmqLogSink>("task_center");
 *     
 *     auto logger = std::make_shared<spdlog::logger>(
 *         "main",
 *         spdlog::sinks_init_list{console_sink, file_sink, zmq_sink});
 *     
 *     logger->set_level(spdlog::level::debug);
 *     spdlog::set_default_logger(logger);
 * }
 * 
 * 
 * // CMakeLists.txt中需要链接ZMQ：
 * target_link_libraries(your_target PRIVATE cppzmq)
 */
