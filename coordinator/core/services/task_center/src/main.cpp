#include <thread>
#include <chrono>
#include <mysql_driver.h>
#include "bootstrap.hpp"
#include "config_parser.hpp"
#include "state_subscriber.hpp"
#include "mode/mode_service.hpp"
#include "component/navigate_executor/navigator.hpp"
#include "component/action_executor/throughout_broadcaster.hpp"
#include "robot_task/robot_task_coordinator.hpp"
#include "internal_interaction/internal_interaction_thread.hpp"
#include "humans_interaction/humans_interaction_thread.hpp"
#include "timing_comply_task/scheduled_task_thread.hpp"
#include "startup_task/startup_task_executor.hpp"

static volatile std::sig_atomic_t closure = 0;
static void signal_handler(int) {closure = 1;}

int main() {
    ModeService* mode_service = nullptr;
    try {
        patrol_robot::init_crash_handler("task_center");
        patrol_robot::init_logging("task_center");
        std::signal(SIGTERM, signal_handler);
        std::signal(SIGINT, signal_handler);
        sql::mysql::get_mysql_driver_instance();
        Config* config = Config::getInstance();
        config->loadFromFile("/etc/patrol_robot_h1/config.ini");

        mode_service = &ModeService::instance();
        StateSubscriber::getInstance().start();
        Navigator::instance();
        ThroughoutBroadcaster::getInstance();
        auto& robot_task_coordinator = RobotTaskCoordinator::instance();
        InternalCommandThread internal_interaction_thread;
        UserInteractionThread humans_interaction_thread;
        StartupTaskThread startup_task_executor;
        ScheduledTaskThread scheduled_task_thread;

        robot_task_coordinator.start();
        internal_interaction_thread.start();
        humans_interaction_thread.start();
        startup_task_executor.start();
        scheduled_task_thread.start();

        while (!closure) std::this_thread::sleep_for(std::chrono::milliseconds(200));
        SPDLOG_INFO("收到停止信号, 开始传送数据后退出");
        
        humans_interaction_thread.stop(); // 先停人工命令, 避免新指令进来
        internal_interaction_thread.stop(); // 再停内部命令, 避免新指令进来
        scheduled_task_thread.stop(); // 停止定时任务
        startup_task_executor.stop(); // 停止开机任务
        robot_task_coordinator.stop(); // 后停全局任务协调器, 停止自动任务或充电任务
        StateSubscriber::getInstance().stop(); // 停止状态订阅, 不再接收
        mode_service->stop(); // 最后停止模式服务的后台任务

        SPDLOG_INFO("task_center正常退出");
        spdlog::shutdown();
        return 0;
    } catch (const std::exception& e) {
        if (mode_service) mode_service->stop();
        SPDLOG_CRITICAL("task_center异常: {}", e.what());
        spdlog::shutdown();
        return -1;
    }
}
