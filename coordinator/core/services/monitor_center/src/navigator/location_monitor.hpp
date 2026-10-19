#pragma once
#include <atomic>
#include <chrono>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include "httplib.hpp"
#include "config_parser.hpp"

struct Point {
    double x;
    double y;
};
struct DetectionArea {
    std::vector<Point> points;
    std::string algorithm_name;
    int algorithm_type;
};

class LocationMonitor {
public:
    explicit LocationMonitor();
    ~LocationMonitor();
    void start();
    void stop();
    
private:
    static constexpr int kZmqTimeoutMs = 3000;
    static constexpr int kLingerMs = 1000;
    static constexpr int kSndHwm = 100;
    static constexpr const char* kPerceiverEndpoint = "tcp://10.10.10.30:2430";

    using AlgorithmKey = std::pair<int, std::string>;

    Config* config = Config::getInstance();
    std::atomic<bool> running{false};
    std::thread worker_thread;

    zmq::context_t ctx{1};
    zmq::socket_t socket_web{ctx, zmq::socket_type::push};
    zmq::socket_t socket_android{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_perceiver{ctx, zmq::socket_type::req};

    httplib::Client http = httplib::Client("http://10.10.10.10:8080");
    std::chrono::steady_clock::time_point last_broadcast_time;
    
    nlohmann::json message = {
        {"Header", {
            {"CmdType", "MCUStatusPost"}, 
            {"Sn", config->get_sn()}, 
            {"Time", std::time(nullptr)}
        }},
        {"MCUStatus", {{"ExceptionCode", "50101"}}}
    };
    std::string current_map = "";
    std::unordered_map<std::string, std::vector<DetectionArea>> detection_areas_;
    std::set<AlgorithmKey> active_algorithms_;

    void monitoring();
    
    void handle_location_loss();
    void handle_location_not_lost();

    bool query_detection_area();
    void check_in_detection_area(const std::string& map_name, double x, double y);
    static bool point_on_segment(double x, double y, const Point& a, const Point& b);
    static bool point_in_polygon(double x, double y, const std::vector<Point>& polygon);
    void send_detection_command(const std::string& command, const std::string& algorithm_name, int algorithm_type);
    void rebuild_perceiver_socket();
};