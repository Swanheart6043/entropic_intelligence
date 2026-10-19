#include <map>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include "config_parser.hpp"

class RecordingQueryService {
public:
    RecordingQueryService();
    ~RecordingQueryService();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    zmq::socket_t socket_web{ctx, zmq::socket_type::push};
    std::thread worker_thread;
    std::atomic<bool> running{false};

    // 监听消息
    void message_loop();
    // 查询录像文件
    void handle_view_record(const std::string& channel, const std::string& begin_time, const std::string& end_time);
    // 辅助函数：解析文件名中的时间戳 front_20260114_223433.mp4 -> "2026-01-14 22:34:33"
    std::string parse_filename_to_datetime(const std::string& filename, const std::string& channel);
    // 辅助函数：时间加秒数
    std::string add_seconds_to_datetime(const std::string& datetime, int seconds);
};
