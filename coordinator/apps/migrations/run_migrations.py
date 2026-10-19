#!/usr/bin/env python3
"""
每次启动会:
  1. 跑 0000_bootstrap.sql(建库 + 建 schema_migrations 元表)
  2. 查询已应用版本
  3. 按文件名顺序,把所有未应用的 migration 应用上去

幂等:重复执行只会跳过已应用的 migration,不会重建表或重复插入数据
(前提:每个 migration 文件本身已正确使用 IF NOT EXISTS / INSERT IGNORE)

用法:
  python3 run_migrations.py
"""
import configparser
import os
import re
import subprocess
import sys
import time
from typing import List, Optional, Set, Tuple

# ====================== 配置常量 ======================
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MIGRATIONS_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "migrations"))
CONFIG_PATH = "/etc/patrol_robot_h1/config.ini"

BOOTSTRAP_FILENAME = "0000_bootstrap.sql"
MIGRATION_FILE_PATTERN = re.compile(r"^(\d{4})_[a-z][a-z0-9_]*\.sql$")
MYSQL_CLI = "mysql"

# ====================== 工具函数 ======================
def log(msg: str, level: str = "INFO") -> None:
    colors = {
        "INFO":  "\033[0;36m",
        "OK":    "\033[0;32m",
        "WARN":  "\033[0;33m",
        "ERROR": "\033[0;31m",
    }
    reset = "\033[0m"
    color = colors.get(level, "")
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
    print(f"{color}[{timestamp}] [migration:{level}] {msg}{reset}", flush=True)

# ====================== 配置加载 ======================
class DBConfig:
    def __init__(self, host: str, port: int, user: str, password: str, database: str):
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.database = database

    @classmethod
    def from_ini(cls, config_path: str) -> "DBConfig":
        if not os.path.exists(config_path):
            raise FileNotFoundError(f"配置文件不存在: {config_path}")

        parser = configparser.ConfigParser()
        parser.read(config_path, encoding="utf-8")

        section = None
        for candidate in ("database", "mysql", "db"):
            if parser.has_section(candidate):
                section = candidate
                break
        if section is None:
            raise ValueError(
                f"config.ini 中未找到 [database] / [mysql] / [db] 配置段: {config_path}"
            )

        s = parser[section]
        return cls(
            host=s.get("host", "127.0.0.1"),
            port=s.getint("port", 3306),
            user=s.get("user", "root"),
            password=s.get("password", ""),
            database=s.get("database", "patrol_robots_db"),
        )

# ====================== mysql 客户端调用封装 ======================
class MySQLClient:
    """
    通过 subprocess 调用 mysql 命令行客户端的薄封装。
    安全说明:
      - 密码通过 MYSQL_PWD 环境变量传递,不出现在命令行(避免 ps 看到)
    """
    def __init__(self, cfg: DBConfig):
        self.cfg = cfg
        self._env = os.environ.copy()
        self._env["MYSQL_PWD"] = cfg.password

    def _base_args(self, database: Optional[str] = None) -> List[str]:
        args = [
            MYSQL_CLI,
            f"--host={self.cfg.host}",
            f"--port={self.cfg.port}",
            f"--user={self.cfg.user}",
            "--default-character-set=utf8mb4",
        ]
        if database is not None:
            args.append(database)
        return args

    def execute_file(self, file_path: str, use_database: bool = True) -> None:
        """
        执行一个 .sql 文件。
        use_database=False 时连到 server 不指定 database(用于 bootstrap)
        """
        db = self.cfg.database if use_database else None
        args = self._base_args(database=db)

        with open(file_path, "rb") as f:
            result = subprocess.run(
                args,
                stdin=f,
                capture_output=True,
                env=self._env,
            )
        if result.returncode != 0:
            stderr = result.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(
                f"执行 {os.path.basename(file_path)} 失败 "
                f"(returncode={result.returncode}): {stderr}"
            )

    def execute_query(self, sql: str, use_database: bool = True) -> List[List[str]]:
        """执行一条查询 SQL,返回二维数组。-N -B 参数:无表头、tab 分隔"""
        db = self.cfg.database if use_database else None
        args = self._base_args(database=db) + ["-N", "-B", "-e", sql]

        result = subprocess.run(
            args,
            capture_output=True,
            env=self._env,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(f"查询失败 (returncode={result.returncode}): {stderr}")

        stdout = result.stdout.decode("utf-8", errors="replace")
        rows = []
        for line in stdout.splitlines():
            if not line.strip():
                continue
            rows.append(line.split("\t"))
        return rows

    def execute_statement(self, sql: str, use_database: bool = True) -> None:
        """执行一条无返回值的 SQL"""
        db = self.cfg.database if use_database else None
        args = self._base_args(database=db) + ["-e", sql]

        result = subprocess.run(
            args,
            capture_output=True,
            env=self._env,
        )
        if result.returncode != 0:
            stderr = result.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(f"执行失败 (returncode={result.returncode}): {stderr}")

# ====================== 核心逻辑 ======================
def discover_migrations(migrations_dir: str) -> Tuple[Optional[str], List[Tuple[str, str]]]:
    """扫描 migrations 目录,返回 (bootstrap_path, [(version, file_path), ...])"""
    if not os.path.isdir(migrations_dir):
        raise NotADirectoryError(f"migrations 目录不存在: {migrations_dir}")

    bootstrap_path = None
    migrations: List[Tuple[str, str]] = []

    for filename in sorted(os.listdir(migrations_dir)):
        full_path = os.path.join(migrations_dir, filename)
        if not os.path.isfile(full_path):
            continue
        if not filename.endswith(".sql"):
            continue

        if filename == BOOTSTRAP_FILENAME:
            bootstrap_path = full_path
            continue

        match = MIGRATION_FILE_PATTERN.match(filename)
        if not match:
            log(f"忽略不符合命名规则的文件: {filename}", "WARN")
            continue

        version = match.group(1)
        if version == "0000":
            log(f"忽略保留版本号 0000(应为 bootstrap): {filename}", "WARN")
            continue
        migrations.append((version, full_path))

    # 校验序号连续性(只警告)
    if migrations:
        versions = [int(v) for v, _ in migrations]
        for i in range(1, len(versions)):
            if versions[i] != versions[i - 1] + 1:
                log(
                    f"序号不连续: {versions[i - 1]:04d} 之后跳到 {versions[i]:04d},"
                    f"请检查是否有 migration 文件遗漏",
                    "WARN",
                )
    return bootstrap_path, migrations

def get_applied_versions(client: MySQLClient) -> Set[str]:
    """从 schema_migrations 表读取已应用的版本号集合"""
    rows = client.execute_query("SELECT version FROM schema_migrations")
    return {row[0] for row in rows if row}

def run_bootstrap(client: MySQLClient, bootstrap_path: Optional[str]) -> None:
    """执行 bootstrap:建库 + 建 schema_migrations 元表"""
    log("阶段 1/2: bootstrap(建库 + 建元表)")
    if bootstrap_path is not None:
        log(f"执行 {os.path.basename(bootstrap_path)}")
        # bootstrap 不指定 database,因为目标库可能还不存在
        client.execute_file(bootstrap_path, use_database=False)
    else:
        log("未找到 0000_bootstrap.sql,使用内置默认 bootstrap")
        db = client.cfg.database
        client.execute_statement(
            f"CREATE DATABASE IF NOT EXISTS `{db}` "
            f"CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci",
            use_database=False,
        )
        client.execute_statement(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "  version VARCHAR(32) PRIMARY KEY,"
            "  applied_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci",
            use_database=True,
        )
    log("bootstrap 完成", "OK")

def run_migrations(client: MySQLClient, migrations: List[Tuple[str, str]]) -> None:
    """逐个应用 migration 文件"""
    log(f"阶段 2/2: 应用业务 migration(共 {len(migrations)} 个文件)")

    if not migrations:
        log("  没有可应用的 migration", "INFO")
        return

    applied = get_applied_versions(client)
    log(f"已应用版本: {sorted(applied) if applied else '(无)'}")

    applied_count = 0
    skipped_count = 0
    for version, file_path in migrations:
        filename = os.path.basename(file_path)
        if version in applied:
            log(f"  跳过 {version} ({filename}) — 已应用")
            skipped_count += 1
            continue

        log(f"  应用 {version} ({filename})...")
        client.execute_file(file_path, use_database=True)

        # 兜底:即使 migration 文件忘记写 schema_migrations,runner 补一刀
        # 配合 INSERT IGNORE 不会和文件内的写入冲突
        client.execute_statement(
            f"INSERT IGNORE INTO schema_migrations (version) VALUES ('{version}')",
            use_database=True,
        )

        log(f"  {version} 应用成功", "OK")
        applied_count += 1

    log(f"汇总: 新应用 {applied_count} 个, 跳过 {skipped_count} 个", "OK")

def check_mysql_cli_available() -> None:
    """启动前检查 mysql 命令行客户端是否可用"""
    try:
        result = subprocess.run(
            [MYSQL_CLI, "--version"],
            capture_output=True,
            timeout=5,
        )
        if result.returncode != 0:
            raise RuntimeError("mysql --version 返回非零")
    except FileNotFoundError:
        log(
            f"未找到 mysql 命令行客户端。请安装:sudo apt install mysql-client",
            "ERROR",
        )
        raise
    except (subprocess.TimeoutExpired, RuntimeError) as e:
        log(f"mysql 命令行客户端不可用: {e}", "ERROR")
        raise

# ====================== 入口 ======================
def main() -> int:
    log("=" * 60)
    log("patrol_robot_h1 migration runner")
    log(f"config:         {CONFIG_PATH}")
    log(f"migrations dir: {MIGRATIONS_DIR}")
    log("=" * 60)
    try:
        check_mysql_cli_available()
    except Exception:
        return 1

    try:
        cfg = DBConfig.from_ini(CONFIG_PATH)
    except (FileNotFoundError, ValueError) as e:
        log(f"配置加载失败: {e}", "ERROR")
        return 1
    log(f"目标数据库: {cfg.user}@{cfg.host}:{cfg.port}/{cfg.database}")

    try:
        bootstrap_path, migrations = discover_migrations(MIGRATIONS_DIR)
    except (NotADirectoryError, ValueError) as e:
        log(f"扫描 migration 目录失败: {e}", "ERROR")
        return 1
    log(f"发现 {len(migrations)} 个业务 migration" + (f",bootstrap: {os.path.basename(bootstrap_path)}" if bootstrap_path else "(无 bootstrap 文件)"))

    client = MySQLClient(cfg)
    try:
        run_bootstrap(client, bootstrap_path)
    except RuntimeError as e:
        log(f"bootstrap 失败: {e}", "ERROR")
        log("提示:检查 host/port/user/password 是否正确,以及 MySQL 是否在运行", "WARN")
        return 2

    try:
        run_migrations(client, migrations)
    except RuntimeError as e:
        log(f"migration 执行中断: {e}", "ERROR")
        log("数据库可能处于半应用状态,请检查 schema_migrations 表后再决定下一步", "WARN")
        return 3

    log("=" * 60)
    log("所有 migration 处理完成", "OK")
    log("=" * 60)
    return 0

if __name__ == "__main__":
    sys.exit(main())