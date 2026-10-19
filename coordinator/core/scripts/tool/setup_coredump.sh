#!/bin/bash
#
# setup_coredump.sh
# 配置系统 coredump 设置，用于巡检机器人项目
# 用法: sudo ./setup_coredump.sh [install|status|test]

set -e

# ==================== 配置区域 ====================
# 根据实际项目路径修改
PROJECT_DIR="/home/heyn/patrol_robot_h1"
CORE_DIR="${PROJECT_DIR}/crashes/cores"
CRASH_DIR="${PROJECT_DIR}/crashes/reports"
CORE_PATTERN="${CORE_DIR}/core.%e.%p.%t"

# Core dump 大小限制 (字节)，防止撑爆磁盘
# unlimited = 不限制, 或指定大小如 209715200 (200MB)
MAX_CORE_SIZE="209715200"  # 200MB

# 保留最近多少天的 core 文件
CORE_RETENTION_DAYS=7

# 运行程序的用户
RUN_USER="heyn"
# ================================================

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "此脚本需要 root 权限运行"
        exit 1
    fi
}

show_status() {
    echo "========================================"
    echo "  Coredump 配置状态"
    echo "========================================"
    echo ""
    
    echo "项目目录: ${PROJECT_DIR}"
    echo "运行用户: ${RUN_USER}"
    echo ""
    
    echo "Core Pattern:"
    echo "  当前: $(cat /proc/sys/kernel/core_pattern)"
    echo "  期望: ${CORE_PATTERN}"
    if [[ "$(cat /proc/sys/kernel/core_pattern)" == "${CORE_PATTERN}" ]]; then
        echo -e "  状态: ${GREEN}✓ 已配置${NC}"
    else
        echo -e "  状态: ${YELLOW}✗ 未配置${NC}"
    fi
    echo ""
    
    echo "Core Size Limit:"
    echo "  配置值: ${MAX_CORE_SIZE} bytes ($((MAX_CORE_SIZE / 1024 / 1024)) MB)"
    echo "  Soft: $(ulimit -Sc)"
    echo "  Hard: $(ulimit -Hc)"
    echo ""
    
    echo "Core 目录: ${CORE_DIR}"
    if [[ -d "${CORE_DIR}" ]]; then
        echo -e "  状态: ${GREEN}存在${NC}"
        echo "  权限: $(stat -c '%a %U:%G' ${CORE_DIR})"
        echo "  文件数: $(find ${CORE_DIR} -name 'core.*' 2>/dev/null | wc -l)"
        echo "  总大小: $(du -sh ${CORE_DIR} 2>/dev/null | cut -f1)"
    else
        echo -e "  状态: ${YELLOW}不存在${NC}"
    fi
    echo ""
    
    echo "Crash 报告目录: ${CRASH_DIR}"
    if [[ -d "${CRASH_DIR}" ]]; then
        echo -e "  状态: ${GREEN}存在${NC}"
        echo "  权限: $(stat -c '%a %U:%G' ${CRASH_DIR})"
        echo "  文件数: $(find ${CRASH_DIR} -name 'crash_*.log' 2>/dev/null | wc -l)"
    else
        echo -e "  状态: ${YELLOW}不存在${NC}"
    fi
    echo ""
    
    # 检查 apport 是否启用（Ubuntu 特有）
    echo "Apport 状态:"
    if command -v systemctl &> /dev/null; then
        if systemctl is-active --quiet apport 2>/dev/null; then
            echo -e "  ${YELLOW}⚠ apport 正在运行，会拦截 core dump${NC}"
        else
            echo -e "  ${GREEN}✓ apport 已禁用${NC}"
        fi
    else
        echo "  N/A (非 systemd 系统)"
    fi
    echo ""
    
    # 检查 limits 配置
    echo "Limits 配置:"
    if [[ -f /etc/security/limits.d/90-coredump.conf ]]; then
        echo -e "  ${GREEN}✓ /etc/security/limits.d/90-coredump.conf 存在${NC}"
    else
        echo -e "  ${YELLOW}✗ limits 未配置${NC}"
    fi
    
    # 检查清理任务
    echo ""
    echo "自动清理任务:"
    if [[ -x /etc/cron.daily/patrol-robot-cleanup ]]; then
        echo -e "  ${GREEN}✓ cron 任务已配置${NC}"
        echo "  保留天数: ${CORE_RETENTION_DAYS} 天"
    else
        echo -e "  ${YELLOW}✗ cron 任务未配置${NC}"
    fi
}

install_config() {
    check_root
    
    log_info "创建目录..."
    mkdir -p "${CORE_DIR}"
    mkdir -p "${CRASH_DIR}"
    
    # 设置目录权限，让运行用户可写
    chown -R "${RUN_USER}:${RUN_USER}" "${PROJECT_DIR}/crashes"
    chmod 755 "${CORE_DIR}"
    chmod 755 "${CRASH_DIR}"
    
    log_info "配置 core_pattern..."
    echo "${CORE_PATTERN}" > /proc/sys/kernel/core_pattern
    
    # 持久化配置
    log_info "写入 sysctl 配置..."
    cat > /etc/sysctl.d/60-coredump.conf << EOF
# Patrol Robot H1 - Coredump 配置
# 项目目录: ${PROJECT_DIR}
kernel.core_pattern = ${CORE_PATTERN}
kernel.core_uses_pid = 1
# 对于 setuid 程序也生成 core dump (可选，生产环境建议关闭)
# fs.suid_dumpable = 2
EOF
    
    sysctl -p /etc/sysctl.d/60-coredump.conf
    
    # 禁用 apport（Ubuntu）
    if [[ -f /etc/default/apport ]]; then
        log_info "禁用 apport..."
        sed -i 's/enabled=1/enabled=0/' /etc/default/apport
        systemctl stop apport 2>/dev/null || true
        systemctl disable apport 2>/dev/null || true
    fi
    
    # 配置 limits - 针对特定用户
    log_info "配置 ulimit..."
    cat > /etc/security/limits.d/90-coredump.conf << EOF
# Patrol Robot H1 - Core dump limits
# 限制大小: ${MAX_CORE_SIZE} bytes

# 特定用户
${RUN_USER} soft core ${MAX_CORE_SIZE}
${RUN_USER} hard core ${MAX_CORE_SIZE}

# root 用户 (用于 systemd 服务)
root soft core ${MAX_CORE_SIZE}
root hard core ${MAX_CORE_SIZE}
EOF
    
    # 创建清理脚本
    log_info "配置自动清理任务..."
    cat > /etc/cron.daily/patrol-robot-cleanup << EOF
#!/bin/bash
# Patrol Robot H1 - 自动清理崩溃文件
# 清理超过 ${CORE_RETENTION_DAYS} 天的 core dump 文件
find ${CORE_DIR} -name 'core.*' -mtime +${CORE_RETENTION_DAYS} -delete 2>/dev/null

# 保留最新的 20 个 crash 报告
cd ${CRASH_DIR} 2>/dev/null && ls -t crash_*.log 2>/dev/null | tail -n +21 | xargs -r rm --

# 记录清理日志
echo "\$(date '+%Y-%m-%d %H:%M:%S') 清理完成" >> ${PROJECT_DIR}/crashes/cleanup.log
EOF
    chmod +x /etc/cron.daily/patrol-robot-cleanup
    
    # 如果使用 systemd 运行服务，还需要配置 systemd
    if [[ -d /etc/systemd/system ]]; then
        log_info "创建 systemd override 配置..."
        mkdir -p /etc/systemd/system/patrol-robot.service.d
        cat > /etc/systemd/system/patrol-robot.service.d/coredump.conf << EOF
# 允许服务生成 core dump
[Service]
LimitCORE=${MAX_CORE_SIZE}
EOF
        systemctl daemon-reload 2>/dev/null || true
    fi
    
    log_info "配置完成！"
    echo ""
    show_status
}

test_coredump() {
    log_info "编译测试程序..."
    
    TEST_DIR=$(mktemp -d)
    cat > "${TEST_DIR}/crash_test.cpp" << 'EOF'
#include <iostream>
#include <csignal>

int main() {
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "准备触发段错误..." << std::endl;
    
    // 故意触发段错误
    int* p = nullptr;
    *p = 42;
    
    return 0;
}
EOF
    
    g++ -g -O0 "${TEST_DIR}/crash_test.cpp" -o "${TEST_DIR}/crash_test"
    
    log_info "运行测试程序..."
    echo ""
    
    # 确保 core dump 启用
    ulimit -c unlimited
    
    # 运行并预期崩溃
    set +e
    "${TEST_DIR}/crash_test"
    EXIT_CODE=$?
    set -e
    
    echo ""
    if [[ $EXIT_CODE -eq 139 ]] || [[ $EXIT_CODE -eq 134 ]]; then
        log_info "程序按预期崩溃 (exit code: ${EXIT_CODE})"
    else
        log_warn "异常退出码: ${EXIT_CODE}"
    fi
    
    # 检查 core 文件
    sleep 1
    CORE_FILES=$(find "${CORE_DIR}" -name 'core.crash_test.*' -mmin -1 2>/dev/null | head -1)
    
    if [[ -n "${CORE_FILES}" ]]; then
        log_info "Core dump 已生成: ${CORE_FILES}"
        log_info "文件大小: $(du -h "${CORE_FILES}" | cut -f1)"
        echo ""
        log_info "使用以下命令分析:"
        echo "  gdb ${TEST_DIR}/crash_test ${CORE_FILES}"
    else
        log_warn "未找到 core 文件，请检查配置"
        echo "当前 core_pattern: $(cat /proc/sys/kernel/core_pattern)"
    fi
    
    # 清理
    rm -rf "${TEST_DIR}"
}

show_usage() {
    echo "用法: $0 [命令]"
    echo ""
    echo "命令:"
    echo "  install   安装并配置 coredump（需要 root）"
    echo "  status    显示当前配置状态"
    echo "  test      测试 coredump 是否正常工作"
    echo ""
    echo "示例:"
    echo "  sudo $0 install"
    echo "  $0 status"
}

# 主入口
case "${1:-status}" in
    install)
        install_config
        ;;
    status)
        show_status
        ;;
    test)
        test_coredump
        ;;
    -h|--help)
        show_usage
        ;;
    *)
        log_error "未知命令: $1"
        show_usage
        exit 1
        ;;
esac
