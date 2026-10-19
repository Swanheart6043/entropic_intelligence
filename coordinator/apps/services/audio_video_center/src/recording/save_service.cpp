#include <cmath>
#include <spdlog/spdlog.h>
#include "save_service.hpp"

RecordingService::RecordingService() {
    SPDLOG_INFO("录像服务已实例化，尚未启动");
}

RecordingService::~RecordingService() {
    stop();
    SPDLOG_INFO("录像服务已停止");
}

void RecordingService::start() {
    if (running.load()) {
        SPDLOG_WARN("录像服务已在运行");
        return;
    }

    // 启动磁盘监控
    DiskMonitor::Config disk_config;
    disk_config.monitor_path = "/datas";
    disk_config.recordings_dir = "/datas/recordings";
    disk_config.warning_percent = 70;
    disk_config.critical_percent = 80;
    disk_config.safe_percent = 60;
    disk_config.min_file_size_mb = 10;
    disk_config.check_interval_sec = 30;
    disk_monitor.start(disk_config);

    running.store(true);

    // 为每个摄像头创建上下文并启动专属线程
    for (const auto& cam : cameras) {
        auto ctx = std::make_unique<CameraContext>();
        ctx->config = make_recorder_config(cam);
        
        // 启动线程（传引用，ctx 生命周期由 camera_contexts_ 管理）
        auto& ctx_ref = *ctx;
        std::string name = cam.name;
        ctx->thread = std::thread(&RecordingService::camera_loop, this,
            std::ref(ctx_ref), name);

        camera_contexts_[cam.name] = std::move(ctx);
    }

    SPDLOG_INFO("录像服务已启动，{} 个摄像头线程运行中", cameras.size());
}

void RecordingService::stop() {
    if (!running.load()) return;
    running.store(false);

    // 通知所有 recorder 停止（中断阻塞的 run()）
    for (auto& [name, ctx] : camera_contexts_) {
        ctx->recorder.request_stop();
    }

    // 等待所有线程退出
    for (auto& [name, ctx] : camera_contexts_) {
        if (ctx->thread.joinable()) {
            ctx->thread.join();
        }
    }
    camera_contexts_.clear();

    disk_monitor.stop();

    SPDLOG_INFO("录像服务已停止");
}

void RecordingService::camera_loop(CameraContext& ctx, const std::string& name) {
    int fail_count = 0;
    bool was_failing = false;
    auto failure_started = std::chrono::steady_clock::now();
    auto last_summary = failure_started;

    while (running.load()) {
        // 检查磁盘空间，满了就等
        auto disk = disk_monitor.get_disk_info();
        if (disk.used_percent >= 80) {
            SPDLOG_WARN("[{}] 磁盘使用率 {}%, 等待清理后再启动", name, disk.used_percent);
            // 以1秒粒度等待，方便响应 stop
            for (int i = 0; i < 10 && running.load(); i++) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            continue;
        }

        // 重试前等待（指数退避）
        int delay_sec = get_retry_delay(fail_count);
        for (int i = 0; i < delay_sec && running.load(); i++) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        if (!running.load()) break;

        // 运行录像（阻塞，直到流断开、磁盘满、或被 request_stop）
        bool ok = ctx.recorder.run(ctx.config);
        if (ok) {
            if (was_failing) {
                auto down = std::chrono::duration_cast<std::chrono::minutes>(
                    std::chrono::steady_clock::now() - failure_started
                ).count();
                SPDLOG_INFO(
                    "[{}] 录像恢复，中断 {} 分钟，期间失败 {} 次",
                    name, 
                    down, 
                    fail_count
                );
                was_failing = false;
            }
            fail_count = 0; // 录像分段结束属于正常情况，不打日志
        } else {
            if (!was_failing) {
                SPDLOG_ERROR(
                    "[{}] 录像启动失败: {}，进入重试模式", 
                    name,
                    ctx.recorder.get_last_error()
                );
                was_failing = true;
                failure_started = std::chrono::steady_clock::now();
                last_summary = failure_started;
            }
            fail_count++;

            auto now = std::chrono::steady_clock::now();
            if (now - last_summary >= std::chrono::minutes(15)) {
                auto down = std::chrono::duration_cast<std::chrono::minutes>(
                    now - failure_started
                ).count();
                SPDLOG_WARN(
                    "[{}] 仍未恢复({}): 已重试 {} 次 / {} 分钟",
                    name, 
                    ctx.recorder.get_last_error(),
                    fail_count,
                    down
                );
                last_summary = now;
            }
        }
    }
    SPDLOG_INFO("[{}] 摄像头线程退出", name);
}

RtspRecorder::Config RecordingService::make_recorder_config(const CameraConfig& cam) const {
    RtspRecorder::Config config;
    config.rtsp_url = "rtsp://admin:heyn_888@" + cam.ip + ":" + cam.port + cam.path;
    config.output_dir = "/datas/recordings/" + cam.name;
    config.file_prefix = cam.name;
    config.segment_duration_sec = 1200;
    config.disk_mount_point = "/datas";
    config.disk_critical_percent = 80;
    return config;
}

int RecordingService::get_retry_delay(int fail_count) const {
    if (fail_count <= 0) return 0;
    // base=5, max=60, 2^4=16 时 5*16=80 已超 max，指数封顶 4 即可
    int shift = std::min(fail_count - 1, 4);
    int delay = base_retry_delay_sec << shift;
    return std::min(delay, max_retry_delay_sec);
}

bool RecordingService::is_running() const {
    return running.load();
}