#include <string>
#include <zmq.hpp>
#include "common/wss_connector.hpp"
#include "config_parser.hpp"

class AudioController {
public:
    explicit AudioController();
    void notification_edge_gateway(const std::string& message);
    void notification_streaming(WebSocketClient& websocket_connect, const std::string& message);
    void handle_video_file_list(WebSocketClient& websocket_connect, const std::string& message);
    void handle_video_file_streaming(WebSocketClient& websocket_connect, const std::string& message);
    void handle_video_file_control(WebSocketClient& websocket_connect, const std::string& message);
    void handle_skeleton_streaming(WebSocketClient& websocket_connect, const std::string& message);
    
private:
    Config* config = Config::getInstance();    
    zmq::context_t ctx{1};
    zmq::socket_t socket_streaming{ctx, zmq::socket_type::push};
    zmq::socket_t socket_recording_center_files_query{ctx, zmq::socket_type::push};
    zmq::socket_t socket_recording_center_file_streaming{ctx, zmq::socket_type::push};
    zmq::socket_t zmq_socket_publisher{ctx, zmq::socket_type::pub};
};
