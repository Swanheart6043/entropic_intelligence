#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "httplib.hpp"
#include "sole.hpp"
#include "broadcast_template.hpp"
#include "mode/mode_service.hpp"
#include "state_subscriber.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "patrol.hpp"
#include "component/ptz_controller.hpp"
#include "patrol_state_machine.hpp"
#include "battery_config_dao.hpp"

Patrol::Patrol() {
    config = Config::getInstance();
    http = std::make_unique<httplib::Client>(httplib::Client("http://10.10.10.10:8080"));
    http->set_keep_alive(false);
    http->set_connection_timeout(5);
    http->set_read_timeout(5);

    int linger_report = 3000;
    int linger_misc = 1000;
    
    zmq_socket.setsockopt(ZMQ_LINGER, &linger_report, sizeof(linger_report));
    socket_android.setsockopt(ZMQ_LINGER, &linger_misc, sizeof(linger_misc));
    socket_publisher.setsockopt(ZMQ_LINGER, &linger_misc, sizeof(linger_misc));

    zmq_socket.connect("tcp://127.0.0.1:2007");
    socket_android.connect("ipc://@edge_gateway_push_pull");
    socket_publisher.connect("ipc://@edge_gateway_sub_task_center");
}

PatrolTask Patrol::query_task() {
    PatrolTask patrol;
    if (!config) {
        SPDLOG_ERROR("查询任务时，配置文件找不到! ");
        return patrol;
    }
    try {
        std::string query = R"(
            SELECT
                rp.line_id, rp.point_name, rp.map_name, rp.action_group_id, rp.x, rp.y, rp.angle,
                p.id, p.plan_id, p.plan_name, p.execution_mode, p.start_time, p.weekly, p.end_time, p.gap, p.is_finish_soon, p.execution_status
            FROM 
                plans_points rp
            LEFT JOIN 
                plans p ON rp.line_id = p.line_id
            WHERE
                p.execution_status = 'PENDING'
                AND FIND_IN_SET(WEEKDAY(CURDATE()) + 1, p.weekly) > 0
                AND (
                    -- ONCE模式：今天没有执行过
                    (
                        p.execution_mode = 'ONCE'
                        AND TIME_TO_SEC(TIMEDIFF(CURTIME(), p.start_time)) BETWEEN 0 AND 5
                        AND (p.last_execution_time IS NULL OR DATE(p.last_execution_time) < CURDATE())
                    )
                    OR
                    -- REPEAT模式：当前时间在start_time和end_time之间，且今天没有执行过
                    (
                        p.execution_mode = 'REPEAT' 
                        AND CURTIME() >= p.start_time 
                        AND CURTIME() <= p.end_time
                        AND (p.last_execution_time IS NULL OR DATE(p.last_execution_time) < CURDATE())
                    )
                )
            ORDER BY
                rp.id
        )";
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(query));
        bool first_row = true;
        while (res->next()) {
            if (first_row) {
                patrol.plan_id = res->getInt("plan_id");
                patrol.plan_name = res->getString("plan_name");
                patrol.line_id = res->getInt("line_id");
                patrol.execution_mode = res->getString("execution_mode") == "ONCE" ? plan::ExecutionMode::ONCE : plan::ExecutionMode::REPEAT;
                patrol.start_time = res->getString("start_time");
                patrol.weekly = res->getString("weekly");
                patrol.end_time = res->getString("end_time");
                patrol.gap = res->getString("gap") == "" ? 0 : std::stoi(res->getString("gap"));
                patrol.is_finish_soon = res->getString("is_finish_soon") == "1" ? true : false;

                first_row = false;
            }
            PatrolTaskPoint point;
            point.map_name = res->getString("map_name");
            point.line_id = res->getInt("line_id");
            point.point_name = res->getString("point_name");
            point.x = res->isNull("x") ? "<NULL>" : res->getString("x");
            point.y = res->isNull("y") ? "<NULL>" : res->getString("y");
            point.angle = res->isNull("angle") ? "<NULL>" : res->getString("angle");
            point.action_group_id = res->isNull("action_group_id") ? 0 : res->getInt("action_group_id");
            patrol.point_list.push_back(point);
        }
        return patrol;
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("数据库错误: {}, 错误代码: {}, SQL状态: {}", e.what(), e.getErrorCode(), e.getSQLState());
        return patrol;
    }
}

void Patrol::mark_task_started(int plan_id) {
    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string update_sql = "UPDATE plans SET last_execution_time = NOW() WHERE plan_id = ?";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(update_sql));
        pstmt->setInt(1, plan_id);
        pstmt->executeUpdate();
        SPDLOG_INFO("标记任务开始执行, plan_id: {}", plan_id);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("标记任务开始失败: {}", e.what());
    }
}

void Patrol::clear_task_started(int plan_id) {
    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string sql = "UPDATE plans SET last_execution_time = NULL WHERE plan_id = ?";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(sql));
        pstmt->setInt(1, plan_id);
        pstmt->executeUpdate();
        SPDLOG_INFO("已清除任务执行标记, plan_id: {}", plan_id);
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("清除任务标记失败: {}", e.what());
    }
}

bool Patrol::check_pre_patrol(const PatrolContext& ctx) {
    // 检查模式
    if (!ModeService::instance().is_auto_mode()) {
        SPDLOG_WARN("当前为手动模式，跳过巡逻");
        return false;
    }
    
    // 检查定位有无丢失
    httplib::Client http = httplib::Client("http://10.10.10.10:8080");
    auto initialize_result = http.Get("/gs-robot/real_time_data/current_initialize_status");
    if (!initialize_result || initialize_result->status != 200) {
        SPDLOG_WARN("定位丢失，跳过巡逻");
        return false;
    }

    // 检查地图是否匹配
    auto current_map = nlohmann::json::parse(initialize_result->body).at("currentMap").get<std::string>();
    if (!ctx.patrol_info.point_list.empty()) {
        auto& first_point = ctx.patrol_info.point_list[0];
        if (first_point.map_name != current_map) {
            SPDLOG_WARN(
                "巡逻计划地图({})与当前地图({})不匹配，跳过巡逻", 
                first_point.map_name, 
                current_map
            );
            return false;
        }
    }

    nlohmann::json state = StateSubscriber::getInstance().get_full_state();
    int battery = state.value("battery_percentage", 0);
    bool is_stop = state.value("is_stop", false);
    auto battery_config = PowerConfigDao::get();
    auto low = battery_config.battery_low_pct;
    auto high = battery_config.battery_high_pct;
    int threshold = PatrolStateMachine::instance().is_force_patrol() ? low : high;
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

void Patrol::init_patrol(const PatrolContext& ctx) {
    // 设置巡逻速度(仅第一次)
    nlohmann::json speed = {{
        {"namespace", "/strategy/follow_base/speed_level"}, 
        {"type", "int"}, 
        {"value", "3"}
    }};
    auto response = http->Post(
        base_url + "/cmd/modify_robot_param",
        speed.dump(), 
        "application/json"
    );
    if (!response || response->status != 200) return;
    SPDLOG_INFO("巡逻速度设置成功: {}", speed.dump());
    
    // 云台回中(仅第一次)
    PTZController ptz_controller;
    ptz_controller.reset_ptz();

    // 播放语音
    BroadcastTemplate tpl_start_patrol{"patrol.start"};
    nlohmann::json params = {
        {"command", "broadcast-single-text"}, 
        {"content", tpl_start_patrol.get_content("开始巡逻")}
    };
    socket_android.send(zmq::buffer(params.dump()), zmq::send_flags::dontwait);

    // 向其他进程广播，巡逻点位总数
    nlohmann::json message = {
        {"topic", "ready_patrol"},
        {"points", ctx.patrol_info.point_list.size()}
    };
    socket_publisher.send(zmq::buffer(message.dump()), zmq::send_flags::none);
    SPDLOG_INFO("初始化巡逻完成");
}

void Patrol::start_new_cycle(PatrolContext& ctx) {
    // 设置uuid
    sole::uuid id = sole::uuid4();
    std::string uuid_string = id.str();
    ctx.patrol_report_uuid = uuid_string;
    // 上报巡逻
    reporting_data_start_navigation(ctx);
}

NavigationResult Patrol::navigate_to_point(const PatrolTaskPoint& point, const PatrolContext& ctx) {
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
                // 同步状态, 检查结束时间
                if (check_is_equal_or_after_end_time(ctx.patrol_info) && ctx.patrol_info.is_finish_soon) {
                    navigator.sync_control(true, false); // 触发取消
                }
                // 上报
                if (navigator.is_navigating()) {
                    reporting_data_during_navigation(ctx, point);
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        
        auto result = navigator.nav_to_custom_point(
            point.map_name,
            point.x.empty() ? 0.0 : std::stod(point.x),
            point.y.empty() ? 0.0 : std::stod(point.y),
            point.angle.empty() ? 0.0 : std::stod(point.angle)
        );
        monitoring.store(false);
        if (monitor.joinable()) {monitor.join();}
        return result;
    } catch (std::exception &e) {
        std::lock_guard<std::mutex> lock(navigation_mutex);
        is_navigating_ = false;
        SPDLOG_ERROR("导航异常: {}, 点位: {}", e.what(), point.point_name);
        return NavigationResult::FAILED;
    }
}

void Patrol::handle_arrival_point(const PatrolContext& ctx) {
    // 向其他进程广播，巡逻点位总数
    nlohmann::json message = {
        {"topic", "arrival_point"},
        {"point_reached", ctx.point_count},
        {"history_point_count", ctx.history_point_count}
    };
    socket_publisher.send(zmq::buffer(message.dump()), zmq::send_flags::none);
}

void Patrol::end_new_cycle(PatrolContext& ctx) {
    reporting_data_end_navigation(ctx); // 上报巡逻结束
    SPDLOG_INFO("第 {} 趟巡逻数据上报完成", ctx.cycle_count);
}

void Patrol::reporting_data_start_navigation(const PatrolContext& ctx) {    
    try {
        using namespace nlohmann;
        if (ctx.patrol_info.point_list.empty()) {
            SPDLOG_WARN("点位列表为空,跳过上报");
            return;
        }
        json start_plan_report = {
            {"Header", {
                {"CmdType", "PC_PostOn_PatrolState"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"PatrolInfo", {
                {"Uuid", ctx.patrol_report_uuid},
                {"Time", format_now_date_to_string()},
                {"PatrolPlanID", ctx.patrol_info.plan_id},
                {"PatrolID", ctx.patrol_info.line_id},
                {"PatrolState", "1"}, //1开始  3结束
                {"Flag", ""}
            }}
        };
        json start_nav_report = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"CurrentTask", {
                {"TaskInfo", {
                    {"MapName", ctx.patrol_info.point_list[0].map_name},
                    {"PatrolID", ctx.patrol_info.line_id},
                    {"PlanName", ctx.patrol_info.plan_name},
                    {"PointName", ctx.patrol_info.point_list[0].point_name}
                }},
                {"TaskStatus", "start"}, 
                {"TaskType", "patrol"}
            }}
        }; 
        zmq_socket.send(zmq::buffer(start_plan_report.dump()), zmq::send_flags::dontwait);
        zmq_socket.send(zmq::buffer(start_nav_report.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("发送巡逻开始数据成功: {}", ctx.patrol_report_uuid);
    } catch(std::exception &e) {
        SPDLOG_ERROR("发送巡逻开始数据异常: {}", e.what());
    }
}

void Patrol::reporting_data_during_navigation(const PatrolContext& ctx, const PatrolTaskPoint& point) {
    try {
        using namespace nlohmann;
        if (ctx.patrol_info.point_list.empty()) {
            SPDLOG_WARN("点位列表为空,跳过上报");
            return;
        }
        json current_mode = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"CurrentTask", {
                {"TaskInfo", {
                    {"MapName", ctx.patrol_info.point_list[0].map_name},
                    {"PatrolID", point.line_id},
                    {"PlanName", ctx.patrol_info.plan_name},
                    {"PointName", point.point_name}
                }},
                {"TaskStatus", "processing"}, 
                {"TaskType", "patrol"}
            }}
        };
        zmq_socket.send(zmq::buffer(current_mode.dump()), zmq::send_flags::dontwait);
    } catch (std::exception &e) {
        SPDLOG_ERROR("发送巡逻过程数据异常: {}", e.what());
    }
}

void Patrol::reporting_data_end_navigation(const PatrolContext& ctx) {
    try {
        using namespace nlohmann;
        if (ctx.patrol_info.point_list.empty()) {
            SPDLOG_WARN("点位列表为空,跳过上报");
            return;
        }
        json end_nav_report = {
            {"Header", {
                {"CmdType", "PhoenixStatusPost"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"CurrentTask", {
                {"TaskInfo", {
                    {"MapName", ctx.patrol_info.point_list[0].map_name},
                    {"PatrolID", ctx.patrol_info.line_id},
                    {"PlanName", ctx.patrol_info.plan_name},
                    {"PointName", ctx.patrol_info.point_list[0].point_name}
                }},
                {"TaskStatus", "stop"},
                {"TaskType", "standBy"}
            }}
        };
        json end_plan_report = {
            {"Header", {
                {"CmdType", "PC_PostOn_PatrolState"},
                {"Sn", config->get_sn()},
                {"Time", std::time(nullptr)}
            }},
            {"PatrolInfo", {
                {"Uuid", ctx.patrol_report_uuid},
                {"Time", format_now_date_to_string()},
                {"PatrolPlanID", ctx.patrol_info.plan_id},
                {"PatrolID", ctx.patrol_info.line_id},
                {"PatrolState", "3"}, //1开始  3结束
                {"Flag", ""}
            }}
        };
        zmq_socket.send(zmq::buffer(end_nav_report.dump()), zmq::send_flags::dontwait);
        zmq_socket.send(zmq::buffer(end_plan_report.dump()), zmq::send_flags::dontwait);
        SPDLOG_INFO("发送巡逻结束数据成功: {}", ctx.patrol_report_uuid);
    } catch (std::exception &e) {
        SPDLOG_ERROR("发送巡逻结束数据异常: {}", e.what());
    }
}

bool Patrol::check_is_equal_or_after_end_time(const PatrolTask& task) {
    // 解析时间字符串
    auto parse_time = [](const std::string& time_str) {
        int h, m, s;
        char colon;
        std::istringstream iss(time_str);
        iss >> h >> colon >> m >> colon >> s;
        return h * 3600 + m * 60 + s;
    };
    int end_seconds = parse_time(task.end_time);
    // 获取当前秒数
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_time);
    int current_seconds = local_time->tm_hour * 3600 + local_time->tm_min * 60 + local_time->tm_sec;
    return current_seconds >= end_seconds;
}

std::string Patrol::format_now_date_to_string() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d%H%M%S");
    return ss.str();
}
