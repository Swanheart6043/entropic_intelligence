#pragma once
#include <string>
#include <functional>
#include <cstddef>

namespace patrol_robot {
namespace crash {

struct Config {
    /// 崩溃报告存放目录
    std::string crash_dir = "/home/heyn/patrol_robot_h1/crashes/reports";
    
    /// 应用名称，用于崩溃报告文件名
    std::string app_name = "unknown";
    
    /// 是否启用 core dump
    bool enable_core_dump = true;
    
    /// 最大堆栈深度
    int max_stack_depth = 64;
    
    /// Core dump 大小限制（字节），0 表示不限制
    /// 默认 200MB，防止撑爆嵌入式设备磁盘
    size_t max_core_size = 200 * 1024 * 1024;
    
    /// 崩溃前回调（注意：必须是 async-signal-safe 的操作！）
    /// 可用于：写标记文件、关闭硬件等
    std::function<void(int)> pre_crash_callback = nullptr;
};

/// 初始化崩溃处理器
/// @param config 配置参数
/// @note 必须在 main() 最开始调用，早于其他所有初始化
void init(const Config& config);

/// 获取最近一次崩溃文件路径
/// @param crash_dir 崩溃目录
/// @return 最近的崩溃文件路径，如果没有则返回空字符串
/// @note 可在启动时调用，检查上次是否崩溃
std::string get_latest_crash_file(const std::string& crash_dir);

/// 清理旧的崩溃文件
/// @param crash_dir 崩溃目录  
/// @param max_files 最大保留文件数
void cleanup_old_crashes(const std::string& crash_dir, int max_files = 20);

}  // namespace crash
}  // namespace patrol_robot
