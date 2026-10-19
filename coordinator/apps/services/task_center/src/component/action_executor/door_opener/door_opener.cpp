#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "db_connection.hpp"
#include "door_opener.hpp"
#include "./customer/yue_chu_an_xun.hpp"

DoorOpener::DoorOpener() {
    
}

DoorOpener::~DoorOpener() {
    try {
        stop();
    } catch (std::exception &e) {
        SPDLOG_ERROR("开门器析构异常: {}", e.what());
    }
}

void DoorOpener::start(const std::string& location, const std::string& type, int gap) {
    if (location.empty() || type.empty() || gap < 0) {
        SPDLOG_WARN("远程开门，位置和类型不能为空 location={}, type={}", location, type);
        return;
    }

    if (running.load()) {
        SPDLOG_WARN("远程开门已在运行");
        return;
    }
    running.store(true);

    // 任何退出路径（正常 return / 异常）都会复位 running
    struct RunningGuard {
        std::atomic<bool>& flag;
        ~RunningGuard() { 
            flag.store(false); 
        }
    };
    RunningGuard guard{running};

    // 一次新的开门动作，重置取消/暂停标志
    cancelled.store(false);
    paused.store(false);

    // 查客户配置
    try {
        auto* conn = DBConnection::get_instance().get_connection();
        const std::string sql = "SELECT config_value FROM settings_basic WHERE config_key = ?";
        std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
        stmt->setString(1, "remote_open_door_customer");
        std::unique_ptr<sql::ResultSet> rs(stmt->executeQuery());
        if (!rs->next()) {
            SPDLOG_WARN("远程开门，settings_basic 未配置 remote_open_door_customer");
            return;
        }
        customer_name = rs->getString("config_value");
    } catch (sql::SQLException& e) {
        SPDLOG_WARN("远程开门，查客户配置 SQL 异常: {}", e.what());
        return;
    } catch (std::exception& e) {
        SPDLOG_WARN("远程开门，查客户配置异常: {}", e.what());
        return;
    }

    if (customer_name.empty()) {
        SPDLOG_WARN("远程开门，客户名为空");
        return;
    }

    // 发送请求（不同客户不同协议）
    SPDLOG_INFO("远程开门: customer={}, location={}, type={}", customer_name, location, type);
    if (customer_name == "ycax") {
        YueChuAnXun yue_chu_an_xun{};
        yue_chu_an_xun.control(location, type);
        wait(gap);
        return;
    }
}

void DoorOpener::wait(int milliseconds) {
    const int step = 500;
    int waited = 0;

    SPDLOG_INFO("等待中...");
    while (waited < milliseconds && running.load()) {
        int sleep_time = std::min(step, milliseconds - waited);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
        waited += sleep_time;
    }
    SPDLOG_INFO("等待完毕");
}

void DoorOpener::stop() {
    if (!running.load()) {
        SPDLOG_INFO("没有在运行无需停止");
        return;
    };
    running.store(false);
    cancelled.store(true);
    SPDLOG_INFO("停止开门");
}

void DoorOpener::pause() {
    SPDLOG_INFO("暂停开门");
    paused.store(true);
}

void DoorOpener::resume() {
    SPDLOG_INFO("恢复开门");
    paused.store(false);
}

bool DoorOpener::is_running() const {
    return running.load();
}

bool DoorOpener::is_pause() const {
    return paused.load();
}