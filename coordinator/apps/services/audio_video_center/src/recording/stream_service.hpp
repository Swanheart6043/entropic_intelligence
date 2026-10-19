#include <thread>
#include <vector>
#include <zmq.hpp>
#include "config_parser.hpp"
#include "./streamer/file_streamer.hpp"

class RecordingStreamingService {
public:
    RecordingStreamingService();
    ~RecordingStreamingService();
    void start();
    void stop();
    
private:
    Config* config = Config::getInstance();
    zmq::context_t ctx{1};
    zmq::socket_t socket{ctx, zmq::socket_type::pull};
    std::thread worker_thread;
    std::atomic<bool> running{false};
    std::unique_ptr<FileStreamer> streamer_;

    void message_loop();
    void enable_streaming(const std::string& channel, const std::string& file_name);
    void disable_streaming(const std::string& channel);
    std::string match_channel(const std::string& name);
};