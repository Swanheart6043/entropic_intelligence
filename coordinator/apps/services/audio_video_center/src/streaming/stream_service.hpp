#include <map>
#include <thread>
#include <vector>
#include <zmq.hpp>
#include "config_parser.hpp"
#include "rtsp_streamer.hpp"

struct ChannelMapItem {
    std::string ip;
    std::string port;
    std::string path;
    std::string output_path;
};

struct StreamingSession {
    std::unique_ptr<RtspStreamer> streamer;
    std::chrono::steady_clock::time_point start_time;
};

class RtspStreamingService {
public:
    RtspStreamingService();
    ~RtspStreamingService();
    void start();
    void stop();
    
private:
    static constexpr int MAX_STREAMING_MINUTES = 20;

    Config* config = Config::getInstance();
    std::map<std::string, StreamingSession> streamer_map;
    std::map<std::string, ChannelMapItem> channel_map = {
        {"high_definition", {
            "10.10.10.40",
            "554",
            "/ISAPI/Streaming/channels/103",
            "/live/" + config->get_sn() + "_2"
        }},
        {"infrared", {
            "10.10.10.41",
            "554",
            "/cam/realmonitor?channel=1&subtype=0",
            "/live/" + config->get_sn() + "_3"
        }},
        {"front", { 
            "10.10.10.42",
            "554",
            "/id=1&type=0",
            "/live/" + config->get_sn() + "_1" 
        }},
        {"rear", { 
            "10.10.10.43",
            "554",
            "/id=1&type=0",
            "/live/" + config->get_sn() + "_4"
        }},
        {"left", { 
            "10.10.10.44",
            "554",
            "/id=1&type=0",
            "/live/" + config->get_sn() + "_5" 
        }},
        {"right", {
            "10.10.10.45", 
            "554", 
            "/id=1&type=0", 
            "/live/" + config->get_sn() + "_6"
        }}
    };
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    std::thread worker_thread;
    std::atomic<bool> running{false};

    void message_loop();
    void handle_enable_streaming(const std::string& channel);
    void handle_disable_streaming(const std::string& channel);
    void check_streaming_timeout();
};