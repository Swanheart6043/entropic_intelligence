#include <csignal>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dirent.h>
#include <array>
#include <vector>
#include <algorithm>
#include "crash_handler.hpp"

namespace patrol_robot {
namespace crash {
namespace {

// 全局配置（信号处理器需要访问）
Config g_config;

// ==================== Async-Signal-Safe 工具函数 ====================
// 整数转字符串（不使用任何动态内存）
int safe_itoa(long value, char* buf, int buf_size) {
    if (buf_size < 2) return 0;
    
    char temp[32];
    int len = 0;
    bool negative = value < 0;
    
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    
    if (negative) value = -value;
    
    while (value > 0 && len < 30) {
        temp[len++] = '0' + static_cast<char>(value % 10);
        value /= 10;
    }
    
    int pos = 0;
    if (negative && pos < buf_size - 1) {
        buf[pos++] = '-';
    }
    
    for (int i = len - 1; i >= 0 && pos < buf_size - 1; i--) {
        buf[pos++] = temp[i];
    }
    buf[pos] = '\0';
    return pos;
}

// 安全写入字符串
void safe_write(int fd, const char* str) {
    if (str) {
        [[maybe_unused]] auto _ = write(fd, str, strlen(str));
    }
}

// 安全写入整数
void safe_write_int(int fd, long value) {
    char buf[32];
    safe_itoa(value, buf, sizeof(buf));
    safe_write(fd, buf);
}

// 获取信号名称
const char* get_signal_name(int signum) {
    switch (signum) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGBUS:  return "SIGBUS";
        case SIGTRAP: return "SIGTRAP";
        case SIGSYS:  return "SIGSYS";
        default:      return "UNKNOWN";
    }
}

// 获取信号描述
const char* get_signal_desc(int signum) {
    switch (signum) {
        case SIGSEGV: return "Segmentation fault (invalid memory access)";
        case SIGABRT: return "Aborted (abnormal termination)";
        case SIGFPE:  return "Floating point exception";
        case SIGILL:  return "Illegal instruction";
        case SIGBUS:  return "Bus error (invalid address alignment)";
        case SIGTRAP: return "Trace/breakpoint trap";
        case SIGSYS:  return "Invalid system call";
        default:      return "Unknown signal";
    }
}

// 获取 SIGSEGV 的具体原因
const char* get_segv_reason(int si_code) {
    switch (si_code) {
        case SEGV_MAPERR: return "Address not mapped to object";
        case SEGV_ACCERR: return "Invalid permissions for mapped object";
        default:          return "Unknown";
    }
}

// ==================== 核心崩溃处理器 ====================
void crash_signal_handler(int signum, siginfo_t* info, void* /*context*/) {
    // 立即阻止递归崩溃
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        signal(sig, SIG_DFL);
    }

    // 构建崩溃报告文件路径
    char filepath[512];
    char pid_str[16];
    char time_str[32];
    
    time_t now = time(nullptr);
    pid_t pid = getpid();
    
    safe_itoa(pid, pid_str, sizeof(pid_str));
    safe_itoa(static_cast<long>(now), time_str, sizeof(time_str));
    
    // 手动拼接路径: {crash_dir}/crash_{app_name}_{pid}_{timestamp}.log
    filepath[0] = '\0';
    strncat(filepath, g_config.crash_dir.c_str(), sizeof(filepath) - 1);
    strncat(filepath, "/crash_", sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, g_config.app_name.c_str(), sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, "_", sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, pid_str, sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, "_", sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, time_str, sizeof(filepath) - strlen(filepath) - 1);
    strncat(filepath, ".log", sizeof(filepath) - strlen(filepath) - 1);
    
    // 打开崩溃文件
    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        fd = STDERR_FILENO;  // 回退到 stderr
    }
    
    // ===== 写入崩溃报告头 =====
    safe_write(fd, "================================================================================\n");
    safe_write(fd, "                           CRASH REPORT\n");
    safe_write(fd, "================================================================================\n\n");
    
    // 基本信息
    safe_write(fd, "Application : ");
    safe_write(fd, g_config.app_name.c_str());
    safe_write(fd, "\n");
    
    safe_write(fd, "PID         : ");
    safe_write_int(fd, pid);
    safe_write(fd, "\n");
    
    safe_write(fd, "Timestamp   : ");
    safe_write_int(fd, static_cast<long>(now));
    safe_write(fd, "\n\n");
    
    // 信号信息
    safe_write(fd, "--------------------------------------------------------------------------------\n");
    safe_write(fd, "Signal Information\n");
    safe_write(fd, "--------------------------------------------------------------------------------\n");
    
    safe_write(fd, "Signal      : ");
    safe_write(fd, get_signal_name(signum));
    safe_write(fd, " (");
    safe_write_int(fd, signum);
    safe_write(fd, ")\n");
    
    safe_write(fd, "Description : ");
    safe_write(fd, get_signal_desc(signum));
    safe_write(fd, "\n");
    
    // 附加信息
    if (info) {
        safe_write(fd, "si_code     : ");
        safe_write_int(fd, info->si_code);
        safe_write(fd, "\n");
        
        if (signum == SIGSEGV) {
            safe_write(fd, "Reason      : ");
            safe_write(fd, get_segv_reason(info->si_code));
            safe_write(fd, "\n");
        }
        
        safe_write(fd, "Fault Addr  : 0x");
        // 手动转换地址为十六进制
        char addr_buf[20];
        unsigned long addr = reinterpret_cast<unsigned long>(info->si_addr);
        static const char hex_chars[] = "0123456789abcdef";
        int pos = 0;
        for (int i = (sizeof(unsigned long) * 2) - 1; i >= 0; i--) {
            addr_buf[pos++] = hex_chars[(addr >> (i * 4)) & 0xF];
        }
        addr_buf[pos] = '\0';
        safe_write(fd, addr_buf);
        safe_write(fd, "\n");
    }
    
    // ===== 堆栈跟踪 =====
    safe_write(fd, "\n--------------------------------------------------------------------------------\n");
    safe_write(fd, "Stack Trace\n");
    safe_write(fd, "--------------------------------------------------------------------------------\n");
    
    std::array<void*, 64> bt_buffer{};
    int bt_depth = backtrace(bt_buffer.data(), std::min(g_config.max_stack_depth, static_cast<int>(bt_buffer.size())));
    backtrace_symbols_fd(bt_buffer.data(), bt_depth, fd);
    
    // ===== 提示信息 =====
    safe_write(fd, "\n--------------------------------------------------------------------------------\n");
    safe_write(fd, "Debug Instructions\n");
    safe_write(fd, "--------------------------------------------------------------------------------\n");
    safe_write(fd, "To decode addresses, use:\n");
    safe_write(fd, "  addr2line -e /path/to/");
    safe_write(fd, g_config.app_name.c_str());
    safe_write(fd, " -f <address>\n\n");
    safe_write(fd, "Or with GDB:\n");
    safe_write(fd, "  gdb /path/to/");
    safe_write(fd, g_config.app_name.c_str());
    safe_write(fd, " core.<pid>\n");
    safe_write(fd, "  (gdb) bt full\n\n");
    safe_write(fd, "================================================================================\n");
    
    // 同步并关闭文件
    if (fd != STDERR_FILENO) {
        fsync(fd);
        close(fd);
    }
    
    // 同时输出到 stderr
    safe_write(STDERR_FILENO, "\n\n[FATAL] ");
    safe_write(STDERR_FILENO, g_config.app_name.c_str());
    safe_write(STDERR_FILENO, " crashed with ");
    safe_write(STDERR_FILENO, get_signal_name(signum));
    safe_write(STDERR_FILENO, "\n");
    safe_write(STDERR_FILENO, "Crash report saved to: ");
    safe_write(STDERR_FILENO, filepath);
    safe_write(STDERR_FILENO, "\n\n");
    
    backtrace_symbols_fd(bt_buffer.data(), bt_depth, STDERR_FILENO);
    
    // 调用用户回调（如果有）
    if (g_config.pre_crash_callback) {
        g_config.pre_crash_callback(signum);
    }
    
    // 重新触发信号，生成 core dump
    raise(signum);
}

// 确保目录存在
bool ensure_directory(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path.c_str(), 0755) == 0;
}
}

// ==================== 公开接口实现 ====================
void init(const Config& config) {
    g_config = config;
    
    // 确保崩溃目录存在
    ensure_directory(config.crash_dir);
    
    // 配置 core dump
    if (config.enable_core_dump) {
        struct rlimit limit{};
        if (config.max_core_size == 0) {
            // 不限制
            limit.rlim_cur = RLIM_INFINITY;
            limit.rlim_max = RLIM_INFINITY;
        } else {
            // 限制大小
            limit.rlim_cur = config.max_core_size;
            limit.rlim_max = config.max_core_size;
        }
        setrlimit(RLIMIT_CORE, &limit);
    }
    
    // 注册信号处理器
    struct sigaction sa{};
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;  // SA_ONSTACK: 使用备用栈，防止栈溢出时无法处理
    sigemptyset(&sa.sa_mask);
    
    // 在处理一个致命信号时，阻塞其他致命信号
    sigaddset(&sa.sa_mask, SIGSEGV);
    sigaddset(&sa.sa_mask, SIGABRT);
    sigaddset(&sa.sa_mask, SIGFPE);
    sigaddset(&sa.sa_mask, SIGILL);
    sigaddset(&sa.sa_mask, SIGBUS);
    
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTRAP}) {
        sigaction(sig, &sa, nullptr);
    }
    
    // 设置备用信号栈（用于处理栈溢出导致的崩溃）
    constexpr size_t ALT_STACK_SIZE = 64 * 1024;  // 64KB
    static char alt_stack_mem[ALT_STACK_SIZE];
    stack_t ss{};
    ss.ss_sp = alt_stack_mem;
    ss.ss_size = sizeof(alt_stack_mem);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);
}

std::string get_latest_crash_file(const std::string& crash_dir) {
    DIR* dir = opendir(crash_dir.c_str());
    if (!dir) return "";
    
    std::string latest_file;
    time_t latest_time = 0;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;
        
        std::string filename = entry->d_name;
        if (filename.find("crash_") != 0) continue;
        if (filename.find(".log") == std::string::npos) continue;
        
        std::string filepath = crash_dir + "/" + filename;
        struct stat st{};
        if (stat(filepath.c_str(), &st) == 0) {
            if (st.st_mtime > latest_time) {
                latest_time = st.st_mtime;
                latest_file = filepath;
            }
        }
    }
    
    closedir(dir);
    return latest_file;
}

void cleanup_old_crashes(const std::string& crash_dir, int max_files) {
    DIR* dir = opendir(crash_dir.c_str());
    if (!dir) return;
    
    std::vector<std::pair<time_t, std::string>> files;
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;
        
        std::string filename = entry->d_name;
        if (filename.find("crash_") != 0) continue;
        
        std::string filepath = crash_dir + "/" + filename;
        struct stat st{};
        if (stat(filepath.c_str(), &st) == 0) {
            files.emplace_back(st.st_mtime, filepath);
        }
    }
    closedir(dir);
    
    if (static_cast<int>(files.size()) <= max_files) return;
    
    // 按时间排序，保留最新的
    std::sort(files.begin(), files.end());
    
    int to_delete = static_cast<int>(files.size()) - max_files;
    for (int i = 0; i < to_delete; i++) {
        unlink(files[i].second.c_str());
    }
}
}
}
