#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <sys/statvfs.h>

/**
 * 独立磁盘监控线程
 * 
 * 职责：
 * 1. 定期检查磁盘使用率
 * 2. 达到警告线时，按文件修改时间从最旧开始删除，直到降到安全水位
 * 3. 优先删除崩溃残留小文件（无观看价值），再删正常旧文件释放空间
 * 4. 达到临界线时设置标志位，供外部轮询
 * 
 * 通知机制：
 *   使用 atomic flag 而非回调，确保所有对 recorder 的操作
 *   都在工作线程内完成，避免跨线程锁
 */
class DiskMonitor {
public:
    struct Config {
        std::string monitor_path = "/datas";
        std::string recordings_dir = "/datas/recordings";
        uint64_t warning_percent = 70;
        uint64_t critical_percent = 80;
        uint64_t safe_percent = 60;
        uint64_t min_file_size_mb = 10;
        int check_interval_sec = 30;
    };

    struct DiskInfo {
        uint64_t total_mb = 0;
        uint64_t available_mb = 0;
        uint64_t used_percent = 0;
    };

    DiskMonitor();
    ~DiskMonitor();

    void start(const Config& config);
    void stop();
    bool is_running() const { return running_.load(); }

    /// 查询是否达到临界水位（供工作线程轮询）
    /// 读取后自动清除标志，调用方需自行处理
    bool is_critical() { return critical_flag_.exchange(false); }

    /// 手动触发一次清理检查
    void trigger_cleanup();

    /// 获取当前磁盘信息（线程安全，内部只有 statvfs 系统调用）
    DiskInfo get_disk_info() const;

private:
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> cleanup_triggered_{false};
    std::atomic<bool> critical_flag_{false};  // 临界水位标志，供外部轮询
    std::thread monitor_thread_;

    void monitor_loop();
    void do_cleanup();
    DiskInfo query_disk_info(const std::string& path) const;
};