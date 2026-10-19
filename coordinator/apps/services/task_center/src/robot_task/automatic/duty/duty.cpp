#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "mode/mode_service.hpp"
#include "state_subscriber.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "duty.hpp"
#include "component/action_executor/action_executor.hpp"
#include "db_connection.hpp"
#include "component/ptz_controller.hpp"
#include "duty_state_machine.hpp"
#include "battery_config_dao.hpp"

Duty::Duty() {
    config = Config::getInstance();
    http = std::make_unique<httplib::Client>(httplib::Client("http://10.10.10.10:8080"));
    http->set_keep_alive(false);
    http->set_connection_timeout(5);
    http->set_read_timeout(5);

    int linger_report = 3000;
    int linger_misc = 1000;

    zmq_socket.setsockopt(ZMQ_LINGER, &linger_report, sizeof(linger_report));
    socket_android.setsockopt(ZMQ_LINGER, &linger_misc, sizeof(linger_misc));
    
    zmq_socket.connect("tcp://127.0.0.1:2007");
    socket_android.connect("ipc://@edge_gateway_push_pull");
}

DutyTask Duty::query_task() {
    DutyTask duty;
    try {
        std::string query = R"(
            SELECT 
                * 
            FROM 
                plans_duty p 
            WHERE 
                CURTIME() BETWEEN p.start_time AND p.end_time 
                AND FIND_IN_SET(WEEKDAY(CURDATE()) + 1, p.weekly) > 0
                AND (p.last_execution_time IS NULL OR DATE(p.last_execution_time) < CURDATE()) 
            ORDER BY 
                p.id
        )";
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::Statement> stmt(conn->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(query));
        while (res->next()) {
            duty.plan_id = res->getInt("duty_plan_id");
            duty.plan_name = res->getString("duty_plan_name");
            duty.start_time = res->getString("start_time");
            duty.end_time = res->getString("end_time");
            duty.weekly = res->getString("weekly");
            duty.gap = res->getInt("gap");
            duty.map_name = res->getString("map_name");
            duty.point_name = res->getString("point_name");
            duty.action_group_id = res->getInt("action_group_id");
        }
        return duty;
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("数据库错误: {}, 错误代码: {}, SQL状态: {}", e.what(), e.getErrorCode(), e.getSQLState());
        return duty;
    }
}

void Duty::mark_task_started(int plan_id) {
    try {
        std::string sql = "UPDATE plans_duty SET last_execution_time = NOW() WHERE duty_plan_id = ?";
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setInt(1, plan_id);
        pstmt->executeUpdate();

        SPDLOG_INFO("标记任务开始执行, duty_plan_id: {}", plan_id);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("标记任务开始失败: {}", e.what());
    }
}

void Duty::clear_task_started(int plan_id) {
    try {
        std::string sql = "UPDATE plans_duty SET last_execution_time = NULL WHERE duty_plan_id = ?";
        auto* conn = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
        pstmt->setInt(1, plan_id);
        pstmt->executeUpdate();
        
        SPDLOG_INFO("已清除任务执行标记, duty_plan_id: {}", plan_id);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("清除任务标记失败: {}", e.what());
    }
}

bool Duty::check_pre_duty(const DutyContext& ctx) {
    // 检查模式
    if (!ModeService::instance().is_auto_mode()) {
        SPDLOG_WARN("当前为手动模式，跳过值守");
        return false;
    }
    
    // 检查定位有无丢失
    httplib::Client http = httplib::Client("http://10.10.10.10:8080");
    auto initialize_result = http.Get("/gs-robot/real_time_data/current_initialize_status");
    if (!initialize_result || initialize_result->status != 200) {
        SPDLOG_WARN("定位丢失，跳过值守");
        return false;
    }

    // 检查地图是否匹配
    auto current_map = nlohmann::json::parse(initialize_result->body).at("currentMap").get<std::string>();
    if (ctx.duty_info.map_name != current_map) {
        SPDLOG_WARN("值守计划地图({})与当前地图({})不匹配", ctx.duty_info.map_name, current_map);
        return false;
    }

    nlohmann::json state = StateSubscriber::getInstance().get_full_state();
    int battery = state.value("battery_percentage", 0);
    bool is_stop = state.value("is_stop", false);
    auto battery_config = PowerConfigDao::get();
    auto low = battery_config.battery_low_pct;
    auto high = battery_config.battery_high_pct;
    int threshold = DutyStateMachine::instance().is_force_duty() ? low : high;
    if (battery < threshold) {
        SPDLOG_WARN("电量过低({}% / {}%)，跳过巡逻", battery, threshold);
        return false;
    }
    if (is_stop) {
        SPDLOG_WARN("急停被按下，跳过巡逻");
        return false;
    }
    return true;
}

NavigationResult Duty::navigate_to_point(const std::string& map_name, const std::string& point_name, const DutyContext& ctx) {
    try {
        // 清除上次的状态
        {
            std::lock_guard<std::mutex> lock(navigation_mutex);
            runtime_status_.reset();
            final_result_.reset();
            is_navigating_ = true;
        }

        auto& navigator = Navigator::instance();
        // 同步 ctx 的取消/暂停状态，同时周期性上报
        std::atomic<bool> monitoring{true};
        std::thread monitor([&]() {
            while (monitoring.load()) {
                // 同步状态
                navigator.sync_control(ctx.should_cancel.load(), false);
                
                // 检查结束时间
                if (check_is_equal_or_after_end_time(ctx.duty_info)) {
                    navigator.sync_control(true, false);  // 触发取消
                }
                
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        auto result = navigator.nav_to_normal_point(map_name, point_name);
        
        monitoring.store(false);
        if (monitor.joinable()) {
            monitor.join();
        }
        return result;
    } catch (std::exception &e) {
        std::lock_guard<std::mutex> lock(navigation_mutex);
        is_navigating_ = false;
        SPDLOG_ERROR("导航异常: {}, 点位: {}", e.what(), point_name);
        return NavigationResult::FAILED;
    }
}

void Duty::report_start_duty(const DutyContext& ctx) {    
    try {
        nlohmann::json start_report = {
            {"Header", {
                {"CmdType", "PC_PostOn_UnattendedlState"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"UnattendedlInfo", {
                {"PlanID", ctx.duty_info.plan_id},
                {"State", "1"}, //1开始  3结束
                {"Uuid", ctx.duty_report_uuid},
                {"Time", format_now_date_to_string()},
            }}
        };
        zmq_socket.send(zmq::buffer(start_report.dump()), zmq::send_flags::dontwait);
    } catch(std::exception &e) {
        SPDLOG_ERROR("上报值守开始异常: {}", e.what());
    }
}

void Duty::report_end_duty(const DutyContext& ctx) {
    try {
        nlohmann::json end_report = {
            {"Header", {
                {"CmdType", "PC_PostOn_UnattendedlState"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"UnattendedlInfo", {
                {"PlanID", ctx.duty_info.plan_id},
                {"State", "3"},
                {"Uuid", ctx.duty_report_uuid},
                {"Time", format_now_date_to_string()},
            }}
        }; 
        zmq_socket.send(zmq::buffer(end_report.dump()), zmq::send_flags::dontwait);
    } catch (std::exception &e) {
        SPDLOG_ERROR("上报值守结束异常: {}", e.what());
    }
}

bool Duty::check_is_equal_or_after_end_time(const DutyTask& task) {
    auto parse_time = [](const std::string& time_str) {
        int h, m, s;
        char colon;
        std::istringstream iss(time_str);
        iss >> h >> colon >> m >> colon >> s;
        return h * 3600 + m * 60 + s;
    };
    int end_seconds = parse_time(task.end_time);

    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);
    int current_seconds = local_time->tm_hour * 3600 + local_time->tm_min * 60 + local_time->tm_sec;

    return current_seconds >= end_seconds;
}

std::string Duty::format_now_date_to_string() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
    return ss.str();
}
