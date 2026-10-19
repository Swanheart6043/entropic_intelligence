#include <spdlog/spdlog.h>
#include <filesystem>
#include <algorithm>
#include <vector>
#include "disk_monitor.hpp"

namespace fs = std::filesystem;

DiskMonitor::DiskMonitor() {

}

DiskMonitor::~DiskMonitor() {
    stop();
}

void DiskMonitor::start(const Config& config) {
    if (running_.load()) {
        SPDLOG_WARN("磁盘监控已在运行");
        return;
    }

    config_ = config;
    stop_requested_.store(false);
    critical_flag_.store(false);
    running_.store(true);
    monitor_thread_ = std::thread(&DiskMonitor::monitor_loop, this);

    SPDLOG_INFO(
        "磁盘监控线程已启动 (警告: {}%, 临界: {}%, 安全: {}%, 间隔: {}秒)", 
        config_.warning_percent, config_.critical_percent, 
        config_.safe_percent, config_.check_interval_sec
    );
}

void DiskMonitor::stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    
    running_.store(false);
    SPDLOG_INFO("磁盘监控线程已停止");
}

void DiskMonitor::trigger_cleanup() {
    cleanup_triggered_.store(true);
}

DiskMonitor::DiskInfo DiskMonitor::get_disk_info() const {
    return query_disk_info(config_.monitor_path);
}

void DiskMonitor::monitor_loop() {
    while (!stop_requested_.load()) {
        auto disk = query_disk_info(config_.monitor_path);

        SPDLOG_DEBUG(
            "磁盘状态: {}% 使用中 ({}GB 可用 / {}GB 总计)",
            disk.used_percent,
            disk.available_mb / 1024,
            disk.total_mb / 1024
        );

        bool need_cleanup = cleanup_triggered_.exchange(false);

        // 达到临界水位 → 设置标志位，供工作线程轮询
        if (disk.used_percent >= config_.critical_percent) {
            SPDLOG_ERROR(
                "磁盘使用率 {}% 达到临界值 {}%!",
                disk.used_percent, config_.critical_percent
            );
            critical_flag_.store(true);
            need_cleanup = true;
        }

        // 达到警告水位 → 执行清理
        if (disk.used_percent >= config_.warning_percent || need_cleanup) {
            SPDLOG_WARN(
                "磁盘使用率 {}% 达到警告值 {}%, 开始清理",
                disk.used_percent, config_.warning_percent
            );
            do_cleanup();
        }

        // 以1秒粒度检查stop
        for (int i = 0; i < config_.check_interval_sec && !stop_requested_.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (cleanup_triggered_.load()) break;
        }
    }
}

void DiskMonitor::do_cleanup() {
    try {
        if (!fs::exists(config_.recordings_dir)) {
            SPDLOG_WARN("录像目录不存在: {}", config_.recordings_dir);
            return;
        }

        std::vector<fs::directory_entry> all_files;
        for (const auto& entry : fs::recursive_directory_iterator(config_.recordings_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
                all_files.push_back(entry);
            }
        }

        if (all_files.empty()) {
            SPDLOG_WARN("没有找到可删除的录像文件");
            return;
        }

        // 分成两组：残留小文件 和 正常文件
        const uint64_t min_size_bytes = config_.min_file_size_mb * 1024 * 1024;

        std::vector<fs::directory_entry> normal_files;
        std::vector<fs::directory_entry> small_files;

        for (const auto& entry : all_files) {
            try {
                if (fs::file_size(entry) < min_size_bytes) {
                    small_files.push_back(entry);
                } else {
                    normal_files.push_back(entry);
                }
            } catch (...) {
                small_files.push_back(entry);
            }
        }

        // 各自按修改时间排序（最旧的在前）
        auto sort_by_time = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            try {
                return fs::last_write_time(a) < fs::last_write_time(b);
            } catch (...) {
                return false;
            }
        };

        std::sort(small_files.begin(), small_files.end(), sort_by_time);
        std::sort(normal_files.begin(), normal_files.end(), sort_by_time);

        // 先删残留小文件（崩溃产物，没有观看价值），再删正常旧文件
        std::vector<fs::directory_entry> delete_order;
        delete_order.reserve(small_files.size() + normal_files.size());
        delete_order.insert(delete_order.end(), small_files.begin(), small_files.end());
        delete_order.insert(delete_order.end(), normal_files.begin(), normal_files.end());

        // 逐个删除，直到降到安全水位
        int deleted_count = 0;
        uint64_t freed_mb = 0;

        for (const auto& file : delete_order) {
            auto disk = query_disk_info(config_.monitor_path);
            if (disk.used_percent <= config_.safe_percent) {
                SPDLOG_INFO("磁盘使用率已降至 {}%, 清理完成", disk.used_percent);
                break;
            }

            if (stop_requested_.load()) break;

            try {
                uint64_t file_size_mb = fs::file_size(file) / 1024 / 1024;
                std::string filename = file.path().filename().string();
                std::string parent = file.path().parent_path().filename().string();

                fs::remove(file);
                deleted_count++;
                freed_mb += file_size_mb;

                SPDLOG_INFO("删除旧录像: {}/{} ({}MB)", parent, filename, file_size_mb);
            } catch (const std::exception& e) {
                SPDLOG_WARN("删除文件失败: {} - {}", file.path().string(), e.what());
            }
        }

        // 清理空目录
        try {
            for (const auto& entry : fs::directory_iterator(config_.recordings_dir)) {
                if (entry.is_directory() && fs::is_empty(entry)) {
                    fs::remove(entry);
                    SPDLOG_DEBUG("删除空目录: {}", entry.path().string());
                }
            }
        } catch (...) {
        }

        auto final_disk = query_disk_info(config_.monitor_path);
        SPDLOG_INFO(
            "清理完成: 删除 {} 个文件, 释放约 {}MB, 当前磁盘使用率: {}%",
            deleted_count, freed_mb, final_disk.used_percent
        );

    } catch (const std::exception& e) {
        SPDLOG_ERROR("磁盘清理异常: {}", e.what());
    }
}

DiskMonitor::DiskInfo DiskMonitor::query_disk_info(const std::string& path) const {
    DiskInfo info;

    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        SPDLOG_ERROR("获取磁盘信息失败: {}", path);
        return info;
    }

    uint64_t total_bytes = stat.f_blocks * stat.f_frsize;
    uint64_t available_bytes = stat.f_bavail * stat.f_frsize;
    uint64_t used_bytes = total_bytes - available_bytes;

    info.total_mb = total_bytes / 1024 / 1024;
    info.available_mb = available_bytes / 1024 / 1024;
    info.used_percent = (total_bytes > 0) ? (used_bytes * 100) / total_bytes : 0;

    return info;
}