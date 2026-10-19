#pragma once
#include <spdlog/spdlog.h>
#include <memory>
#include <optional>
#include <mutex>
#include <condition_variable>
#include <zmq.hpp>
#include <mysql_connection.h>
#include <mysql_driver.h>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/statement.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include "httplib.hpp"
#include "config_parser.hpp"
#include "duty_state_machine_common.hpp"

class Duty {
public:
    Duty();
    
    // 任务查询
    DutyTask query_task();
    void mark_task_started(int plan_id);
    void clear_task_started(int plan_id);
    
    // 值守前检查
    bool check_pre_duty(const DutyContext& ctx);
    
    // 导航相关
    NavigationResult navigate_to_point(const std::string& map_name, const std::string& point_name, const DutyContext& ctx);
    
    // 检查方法
    bool check_is_equal_or_after_end_time(const DutyTask& task);
    std::string format_now_date_to_string();    
    
    // 上报数据
    void report_start_duty(const DutyContext& ctx);
    void report_end_duty(const DutyContext& ctx);

private:
    Config* config;
    std::string base_url = "/gs-robot";
    std::unique_ptr<httplib::Client> http;
    zmq::context_t ctx{1};
    zmq::socket_t zmq_socket{ctx, zmq::socket_type::push};
    zmq::socket_t socket_android{ctx, zmq::socket_type::push};
    
    std::mutex navigation_mutex;
    std::condition_variable navigation_cv;
    std::optional<NavigationRuntimeStatus> runtime_status_;
    std::optional<NavigationFinalResult> final_result_;
    const int MAX_LOCALIZATION_LOST_COUNT = 30;
    bool is_navigating_ = false;
};