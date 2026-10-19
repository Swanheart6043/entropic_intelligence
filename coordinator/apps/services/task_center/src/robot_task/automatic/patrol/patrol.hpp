#pragma once
#include <spdlog/spdlog.h>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <zmq.hpp>
#include "db_connection.hpp"
#include "httplib.hpp"
#include "config_parser.hpp"
#include "patrol_state_machine_common.hpp"

class Patrol {
public:
    Patrol();
    
    // 任务查询
    PatrolTask query_task();
    void mark_task_started(int plan_id);
    void clear_task_started(int plan_id);
    
    // 巡逻前检查
    bool check_pre_patrol(const PatrolContext& ctx);

    // 初始化巡逻
    void init_patrol(const PatrolContext& ctx);

    // 跑躺
    void start_new_cycle(PatrolContext& ctx);
    void end_new_cycle(PatrolContext& ctx);

    // 导航相关
    NavigationResult navigate_to_point(const PatrolTaskPoint& point, const PatrolContext& ctx);
    void handle_arrival_point(const PatrolContext& ctx);

    // 上报数据
    void reporting_data_start_navigation(const PatrolContext& ctx);
    void reporting_data_during_navigation(const PatrolContext& ctx, const PatrolTaskPoint& point);
    void reporting_data_end_navigation(const PatrolContext& ctx);

    // 工具方法
    bool check_is_equal_or_after_end_time(const PatrolTask& task);
    std::string format_now_date_to_string();
    
private:
    Config* config;
    std::string base_url = "/gs-robot";
    
    // HTTP/WebSocket
    std::unique_ptr<httplib::Client> http;
    
    // ZMQ
    zmq::context_t ctx{1};
    zmq::socket_t zmq_socket{ctx, zmq::socket_type::push};
    zmq::socket_t socket_android{ctx, zmq::socket_type::push};
	zmq::socket_t socket_publisher{ctx, zmq::socket_type::pub};
    
    // 导航状态同步
    std::mutex navigation_mutex;
    std::condition_variable navigation_cv;
    std::optional<NavigationRuntimeStatus> runtime_status_;
    std::optional<NavigationFinalResult> final_result_;
    const int MAX_LOCALIZATION_LOST_COUNT = 30;
    
    bool is_navigating_ = false;
};