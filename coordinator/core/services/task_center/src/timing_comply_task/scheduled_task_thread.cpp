#include <memory>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "scheduled_task_thread.hpp"
#include "state_subscriber.hpp"

ScheduledTaskThread::ScheduledTaskThread() {
    socket_publisher.connect("ipc://@edge_gateway_sub_task_center");
}

ScheduledTaskThread::~ScheduledTaskThread() {
    stop();
}

void ScheduledTaskThread::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
    if (running) {
        SPDLOG_WARN("定时任务线程已经在启动了");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(cv_m);
        running = true;
    }
    try {
        worker_thread = std::thread(&ScheduledTaskThread::monitoring_loop, this);
    } catch (...) {
        std::lock_guard<std::mutex> lock(cv_m);
        running = false;
        throw;
    }
    SPDLOG_INFO("定时任务线程启动");
}

void ScheduledTaskThread::stop() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex);
    {
        // 与 wait_for 的谓词检查使用同一把锁，避免丢失停止通知。
        std::lock_guard<std::mutex> lock(cv_m);
        running = false;
    }
    cv.notify_all();
    if (worker_thread.joinable()) {
        worker_thread.join();
        SPDLOG_INFO("定时任务线程停止");
    }
}

void ScheduledTaskThread::monitoring_loop() {
    while (running) {
        try {
            query_scheduled_task();
            executor_scheduled_task();
        } catch (const std::exception& e) {
            SPDLOG_ERROR("定时任务轮询异常，稍后重试: {}", e.what());
        }
        // 空列表、未知任务、数据库失败等情况均按固定间隔重试。
        sleeping(std::chrono::seconds(60));
    }
}

void ScheduledTaskThread::query_scheduled_task() {
    try {
        scheduled_task_list.clear();
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::unique_ptr<sql::Statement> stmt(con->createStatement());
        std::string query = R"(
            SELECT * FROM 
                scheduled_tasks p 
            WHERE 
                CURTIME() >= p.start_time AND CURTIME() <= p.end_time 
                AND (p.last_execution_time IS NULL OR DATE(p.last_execution_time) < CURDATE())
            ORDER BY
                p.id
        )";
        std::unique_ptr<sql::ResultSet> res(stmt->executeQuery(query));

        while (res->next()) {
            ScheduledTask scheduled_task;
            scheduled_task.id = res->getInt("id");
            scheduled_task.type = res->getString("task_type");
            scheduled_task.value = res->getString("task_value");
            scheduled_task_list.push_back(scheduled_task);
        }
    } catch (sql::SQLException &e) {
        scheduled_task_list.clear();
        SPDLOG_ERROR("数据库错误: {}, 错误代码: {}, SQL状态: {}", e.what(), e.getErrorCode(), e.getSQLState());
    }
}

void ScheduledTaskThread::executor_scheduled_task() {
    for (const auto& item : scheduled_task_list) {
        if (!running) break;
        try {
            const bool is_light_task = item.type == "12" || item.type == "14";
            if (item.type != "5" && !is_light_task) {
                SPDLOG_WARN("忽略不支持的定时任务类型, task_id: {}, type: {}", item.id, item.type);
                continue;
            }
            if (is_light_task && item.value != "0" && item.value != "1") {
                SPDLOG_WARN("定时灯光任务开关值无效, task_id: {}, value: {}", item.id, item.value);
                continue;
            }

            bool skip_light_on = false;
            if (is_light_task && item.value == "1") {
                const auto state = StateSubscriber::getInstance().get_full_state();
                const auto charging = state.find("is_charging");
                if (!state.is_object() || charging == state.end() || !charging->is_boolean()) {
                    SPDLOG_WARN("充电状态未就绪，稍后重试定时开灯, task_id: {}", item.id);
                    continue;
                }
                skip_light_on = charging->get<bool>();
            }

            if (!running) break;
            // 对端没有执行回执：下发前记录当天已处理，避免重复下发。
            // 充电时按原有策略跳过当天开灯；此标记不代表设备执行成功。
            if (!mark_task_started(item.id)) continue;
            if (skip_light_on) {
                SPDLOG_INFO("正在充电，跳过当天定时开灯, task_id: {}", item.id);
                continue;
            }

            SPDLOG_INFO("开始下发定时任务, task_id: {}, type: {}, value: {}", item.id, item.type, item.value);
            if (item.type == "5") {
                handle_volume(item.value);
            } else if (item.type == "12") {
                handle_warning_light(item.value);
            } else {
                handle_head_light(item.value);
            }
        } catch (const std::exception& e) {
            SPDLOG_ERROR("处理定时任务异常, task_id: {}: {}", item.id, e.what());
        }
    }
}

// 控制音量
void ScheduledTaskThread::handle_volume(const std::string& volume) {
    nlohmann::json payload = {
        {"topic", "volume_adjustment"},
        {"volume", volume}
    };
    const auto sent = socket_publisher.send(
        zmq::buffer(payload.dump()), 
        zmq::send_flags::dontwait
    );
    if (!sent) {
        SPDLOG_WARN("定时音量指令未进入本地发送队列: {}", volume);
    }
}

// 开关大灯
void ScheduledTaskThread::handle_head_light(const std::string& switch_flag) {
    bool charging = StateSubscriber::getInstance().is_charging();
    if (switch_flag == "1" && !charging) {
        mcu_controller.turn_on_head_light();
        return;
    }
    if (switch_flag == "0") {
        mcu_controller.turn_off_head_light();
        return;
    }
}

// 开关警示灯
void ScheduledTaskThread::handle_warning_light(const std::string& switch_flag) {
    bool charging = StateSubscriber::getInstance().is_charging();
    if (switch_flag == "1" && !charging) {
        mcu_controller.turn_on_warning_light();
        return;
    }
    if (switch_flag == "0") {
        mcu_controller.turn_off_warning_light();
        return;
    }
}

bool ScheduledTaskThread::mark_task_started(int task_id) {
    try {
        sql::Connection* con = DBConnection::get_instance().get_connection();
        std::string update_sql = R"(
            UPDATE scheduled_tasks SET last_execution_time = NOW()
            WHERE id = ?
                AND (last_execution_time IS NULL OR DATE(last_execution_time) < CURDATE())
                AND CURTIME() >= start_time AND CURTIME() <= end_time
        )";
        std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(update_sql));
        pstmt->setInt(1, task_id);
        if (pstmt->executeUpdate() != 1) {
            SPDLOG_INFO("任务已处理、已移除或已超出时间窗口，跳过下发, task_id: {}", task_id);
            return false;
        }
        SPDLOG_INFO("记录定时任务当天已处理, task_id: {}", task_id);
        return true;
    } catch (sql::SQLException &e) {
        SPDLOG_ERROR("标记任务失败，本轮不下发, task_id: {}: {}", task_id, e.what());
        return false;
    }
}

void ScheduledTaskThread::sleeping(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(cv_m);
    cv.wait_for(lock, duration, [this]() {return !running.load();});
}
