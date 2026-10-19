#pragma once
#include <memory>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>
#include <zmq.hpp>
#include "httplib.hpp"
#include "websocket_client.hpp"
#include "config_parser.hpp"
#include "navigation_types.hpp"

class Navigator {
public:
    static Navigator& instance();
    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;
    Navigator(Navigator&&) = delete;
    Navigator& operator=(Navigator&&) = delete;

    /**
     * 导航到地图上的命名导航点
     * @param map_name   地图名
     * @param point_name 导航点名
     * @return 导航结果（SUCCESS/SKIPPED/CANCELLED/...）
     */
    NavigationResult nav_to_normal_point(const std::string& map_name, const std::string& point_name);

    /**
     * 导航到地图上的自定义坐标
     * @param map_name 地图名
     * @param x        栅格 x
     * @param y        栅格 y
     * @param angle    朝向角度
     */
    NavigationResult nav_to_custom_point(const std::string& map_name, int x, int y, int angle);

    // 同步状态机的取消/暂停标志，并唤醒等待中的导航循环
    void sync_control(bool do_cancel, bool do_pause);
    
    // 取消导航
    bool cancel_navigation();
    
    // 暂停导航
    bool pause_navigation();
    
    // 恢复导航
    bool resume_navigation();
    
    // 查看是否导航中
    bool is_navigating() const;
    
    // 查看是否暂停中
    bool is_paused() const;

private:
    static constexpr auto HEARTBEAT_TIMEOUT = std::chrono::seconds(60);
    static constexpr auto LOCALIZATION_LOST_TIMEOUT = std::chrono::seconds(120);
    static constexpr auto RESEND_QUIET_PERIOD = std::chrono::seconds(1);

    struct RuntimeStatus {
        int status_code = 0;
        std::string message;
        std::chrono::steady_clock::time_point last_update;
        std::optional<std::chrono::steady_clock::time_point> localization_lost_start;
    };
    struct PendingCommand {
        bool is_custom = false;
        std::string map_name;
        std::string point_name; // normal 用
        int x = 0, y = 0, angle = 0; // custom 用
    };

    Config* config = Config::getInstance();
    std::string base_url = "/gs-robot";
    std::unique_ptr<httplib::Client> http;
    std::unique_ptr<WebSocketClient> websocket;

    zmq::context_t ctx{1};
    zmq::socket_t socket_to_audio{ctx, zmq::socket_type::push};
    zmq::socket_t socket_to_web{ctx, zmq::socket_type::push};

    std::mutex nav_mutex;
    std::condition_variable nav_cv;
    std::optional<RuntimeStatus> runtime_status;
    std::optional<NavigationResult> final_result;
    std::optional<PendingCommand> pending_command;

    bool lost_resent = false;
    int point_unreachable_report_threshold = 3;
    int consecutive_point_unreachable_count = 0;
    bool last_navigation_was_point_unreachable = false;
    bool point_unreachable_streak_reported = false;
    
    std::atomic<bool> navigating{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> paused{false};

    Navigator();
    ~Navigator();

    // 阻塞等待导航完成，由条件变量驱动
    NavigationResult waiting_for_navigation();

    // WebSocket 状态消息回调
    void listening(const std::string& message);

    // 处理 404/408：目标点不可达，上报 50104，标记 SKIPPED
    void handle_point_unreachable();

    // 处理 702：卡虚拟墙，上报后标记 FAILED
    void handle_in_virtual_wall();

    // 处理 1006：定位丢失，超过 LOCALIZATION_LOST_TIMEOUT 后才判失败
    void handle_location_lost(int code, const std::string& msg, std::chrono::steady_clock::time_point now);

    // 处理未识别的状态码，上报"导航未知异常"
    void handle_nav_unknown_error(int code, const std::string& msg);

    // 重发导航到点
    bool resend_navigation();
};
