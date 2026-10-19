#!/usr/bin/env python3
"""将 [platform] 下的旧 IP/端口配置迁移为完整 URL。
脚本只在对应的新键缺失时生成它；写入成功后删除旧键。首次改动前会在同目录
创建固定名称的备份，重复执行不会重复迁移，也不会覆盖最初的备份。
"""
import argparse
import configparser
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
from typing import Dict, Iterable

DEFAULT_CONFIG_PATH = Path("/etc/patrol_robot_h1/config.ini")
PLATFORM_SECTION = "platform"
LEGACY_KEYS = {
    "wss_server_ip",
    "wss_server_port",
    "wss_server_path",
    "websocket_scheme",
    "http_server_ip",
    "http_server_port",
    "http_server_path",
    "http_server_scheme",
    "rtsp_server_ip",
    "rtsp_server_port",
}

def _required(values: configparser.SectionProxy, *keys: str) -> None:
    missing = [key for key in keys if not values.get(key, "").strip()]
    if missing:
        raise ValueError(f"无法迁移，缺少旧配置: {', '.join(missing)}")

def _normalize_scheme(value: str, allowed: Iterable[str], key: str) -> str:
    normalized = value.strip().lower()
    if normalized not in set(allowed):
        raise ValueError(f"{key} 的协议无效: {value}")
    return normalized

def build_canonical_urls(values: configparser.SectionProxy) -> Dict[str, str]:
    generated: Dict[str, str] = {}

    if not values.get("websocket_url", "").strip():
        _required(values, "wss_server_ip", "wss_server_port")
        scheme = _normalize_scheme(
            values.get("websocket_scheme", "ws"), {"ws", "wss"}, "websocket_scheme"
        )
        path = values.get("wss_server_path", "").strip()
        generated["websocket_url"] = (
            f"{scheme}://{values['wss_server_ip'].strip()}:{values['wss_server_port'].strip()}"
            f"{path}{{sn}}"
        )

    if not values.get("http_base_url", "").strip():
        _required(values, "http_server_ip", "http_server_port")
        scheme = _normalize_scheme(
            values.get("http_server_scheme", "http"), {"http", "https"}, "http_server_scheme"
        )
        path = values.get("http_server_path", "").strip()
        generated["http_base_url"] = (
            f"{scheme}://{values['http_server_ip'].strip()}:{values['http_server_port'].strip()}"
            f"{path}"
        ).rstrip("/")

    if not values.get("rtsp_base_url", "").strip():
        _required(values, "rtsp_server_ip", "rtsp_server_port")
        generated["rtsp_base_url"] = (
            f"rtsp://{values['rtsp_server_ip'].strip()}:{values['rtsp_server_port'].strip()}"
        )

    return generated

def rewrite_platform_section(original: str, generated: Dict[str, str]) -> str:
    lines = original.splitlines(keepends=True)
    section_pattern = re.compile(r"^\s*\[([^]]+)]\s*(?:[#;].*)?$")
    key_pattern = re.compile(r"^\s*([^#;][^=:#]*?)\s*[=:]")
    output = []
    in_platform = False
    inserted = False

    def insert_generated() -> None:
        nonlocal inserted
        if inserted:
            return
        for key, value in generated.items():
            output.append(f"{key} = {value}\n")
        if generated:
            output.append("\n")
        inserted = True

    for line in lines:
        section_match = section_pattern.match(line.strip())
        if section_match:
            if in_platform:
                insert_generated()
            in_platform = section_match.group(1).strip().lower() == PLATFORM_SECTION
            output.append(line)
            continue

        if in_platform:
            key_match = key_pattern.match(line)
            if key_match and key_match.group(1).strip().lower() in LEGACY_KEYS:
                continue
        output.append(line)

    if in_platform:
        insert_generated()
    if not inserted:
        raise ValueError("配置文件中不存在 [platform] 段")
    return "".join(output)

def migrate(config_path: Path, dry_run: bool = False) -> bool:
    if not config_path.is_file():
        raise FileNotFoundError(f"配置文件不存在: {config_path}")

    original = config_path.read_text(encoding="utf-8")
    parser = configparser.ConfigParser(interpolation=None)
    parser.read_string(original)
    if not parser.has_section(PLATFORM_SECTION):
        raise ValueError("配置文件中不存在 [platform] 段")

    platform = parser[PLATFORM_SECTION]
    generated = build_canonical_urls(platform)
    has_legacy = any(key in platform for key in LEGACY_KEYS)
    if not generated and not has_legacy:
        print(f"配置无需迁移: {config_path}")
        return False

    migrated = rewrite_platform_section(original, generated)
    if dry_run:
        for key, value in generated.items():
            print(f"将新增 {key} = {value}")
        print(f"将删除 {sum(1 for key in LEGACY_KEYS if key in platform)} 个旧配置项")
        return True

    backup_path = config_path.with_name(config_path.name + ".pre-platform-url-migration.bak")
    if not backup_path.exists():
        shutil.copy2(config_path, backup_path)

    file_mode = stat.S_IMODE(config_path.stat().st_mode)
    temp_name = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="",
            dir=config_path.parent,
            prefix=config_path.name + ".",
            suffix=".tmp",
            delete=False,
        ) as temp_file:
            temp_name = temp_file.name
            temp_file.write(migrated)
            temp_file.flush()
            os.fsync(temp_file.fileno())
        os.chmod(temp_name, file_mode)
        os.replace(temp_name, config_path)
    finally:
        if temp_name and os.path.exists(temp_name):
            os.unlink(temp_name)

    print(f"配置迁移完成: {config_path}")
    print(f"迁移前备份: {backup_path}")
    return True

def main() -> int:
    argument_parser = argparse.ArgumentParser(description=__doc__)
    argument_parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_PATH)
    argument_parser.add_argument("--dry-run", action="store_true")
    args = argument_parser.parse_args()
    try:
        migrate(args.config, args.dry_run)
        return 0
    except (OSError, ValueError, configparser.Error) as exc:
        print(f"配置迁移失败: {exc}", file=sys.stderr)
        return 1

if __name__ == "__main__":
    sys.exit(main())
