#!/usr/bin/env python3
"""Read-only Linux network evidence collector; Python 3.8+, no extra packages."""

import argparse
import datetime
import json
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading
import time


SERVICES = (
    "NetworkManager.service", "systemd-networkd.service", "mysql.service",
    "mysqld.service", "monitor_center.service", "task_center.service",
    "maintain_system.service",
)
COMMAND_TIMEOUT = 5
OUTPUT_LIMIT = 32768


def read_text(path):
    try:
        return Path(path).read_text(errors="replace").strip()
    except OSError as exc:
        return "unavailable: {}".format(exc)


def run_command(argv):
    """Bound command duration and stored output; never invoke a shell."""
    started = time.monotonic()
    try:
        result = subprocess.run(
            argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=COMMAND_TIMEOUT, encoding="utf-8", errors="replace",
        )
        status, output = result.returncode, result.stdout
    except subprocess.TimeoutExpired as exc:
        status, output = "timeout", exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
    except OSError as exc:
        status, output = "unavailable", str(exc)
    return {
        "argv": argv, "status": status,
        "duration_s": round(time.monotonic() - started, 3),
        "output": output[:OUTPUT_LIMIT], "truncated": len(output) > OUTPUT_LIMIT,
    }


class Collector:
    def __init__(self, output_dir, interval=30, max_bytes=10 * 1024 * 1024, backups=5):
        self.interval = interval
        self.boot_id = read_text("/proc/sys/kernel/random/boot_id")
        self.stop = threading.Event()
        self.network_changed = threading.Event()
        self.loggers = {}
        for name in ("network-events", "system-events", "snapshots"):
            handler = RotatingFileHandler(
                str(output_dir / (name + ".jsonl")), maxBytes=max_bytes,
                backupCount=backups, encoding="utf-8",
            )
            handler.setFormatter(logging.Formatter("%(message)s"))
            logger = logging.Logger(name, level=logging.INFO)
            logger.addHandler(handler)
            self.loggers[name] = logger

    def record(self, stream, **fields):
        entry = {
            "time": datetime.datetime.now().astimezone().isoformat(timespec="milliseconds"),
            "monotonic_s": round(time.monotonic(), 3), "boot_id": self.boot_id,
        }
        entry.update(fields)
        self.loggers[stream].info(json.dumps(entry, ensure_ascii=False))

    def follow(self, stream, argv):
        """Restart a failed observer, not the services being observed."""
        while not self.stop.is_set():
            process = None
            try:
                process = subprocess.Popen(
                    argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    encoding="utf-8", errors="replace", bufsize=1,
                )
                # A separate reader avoids blocking shutdown on readline().
                def read_lines(child=process):
                    for line in child.stdout:
                        self.record(stream, event=line.rstrip()[:OUTPUT_LIMIT])
                        if stream == "network-events":
                            self.network_changed.set()

                reader = threading.Thread(target=read_lines, daemon=True)
                reader.start()
                while process.poll() is None and not self.stop.wait(0.5):
                    pass
                if not self.stop.is_set():
                    self.record(stream, observer_exit=process.returncode)
            except OSError as exc:
                self.record(stream, observer_error=str(exc))
            finally:
                if process is not None:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                    reader.join(timeout=2)
                    if not reader.is_alive():
                        process.stdout.close()
            self.stop.wait(5)

    def snapshot(self, reason):
        snapshot_id = "{:.3f}".format(time.monotonic())
        self.record("snapshots", snapshot=snapshot_id, reason=reason, phase="begin")
        commands = [
            ["ip", "-br", "address"], ["ip", "-s", "link"],
            ["ip", "-4", "route", "show", "table", "all"],
            ["ip", "-4", "rule", "show"],
            # Route lookup is local only; it sends no network probes.
            ["ip", "route", "get", "10.10.10.10"],
            ["ss", "-lntp"], ["ss", "-nto", "state", "syn-sent"],
            ["ps", "-C", "monitor_center,task_center,mysqld,NetworkManager",
             "-o", "pid,ppid,comm,pcpu,pmem,rss,nlwp,etimes,stat"],
            ["systemctl", "show"] + list(SERVICES) + [
                "-p", "Id,ActiveState,SubState,MainPID,NRestarts,ExecMainStatus,"
                "FailureAction,StartLimitAction,OnFailure",
            ],
        ]
        service_pids = set()
        for command in commands:
            if self.stop.is_set():
                break
            result = run_command(command)
            self.record("snapshots", snapshot=snapshot_id, command=result)
            if command[0] == "systemctl":
                for line in result["output"].splitlines():
                    if line.startswith("MainPID=") and line[8:].isdigit():
                        if int(line[8:]) > 0:
                            service_pids.add(line[8:])

        interfaces = {}
        for interface in Path("/sys/class/net").glob("*"):
            interfaces[interface.name] = {
                key: read_text(interface / key)
                for key in ("address", "ifindex", "operstate", "carrier", "speed", "duplex")
            }
        processes = {}
        for pid in sorted(service_pids):
            try:
                fd_count = len(list(Path("/proc", pid, "fd").iterdir()))
            except OSError:
                fd_count = None
            processes[pid] = {"fd_count": fd_count, "status": "\n".join(
                line for line in read_text("/proc/{}/status".format(pid)).splitlines()
                if line.startswith(("Name:", "State:", "VmRSS:", "VmSize:", "Threads:"))
            )}
        self.record(
            "snapshots", snapshot=snapshot_id, phase="end", interfaces=interfaces,
            processes=processes, proc={
                name: read_text("/proc/" + name)
                for name in ("uptime", "loadavg", "meminfo", "net/sockstat", "sys/fs/file-nr")
            },
        )
        # Flush and fsync at checkpoints. Sudden power loss may still lose the tail.
        for logger in self.loggers.values():
            for handler in logger.handlers:
                with handler.lock:
                    handler.flush()
                    os.fsync(handler.stream.fileno())

    def run(self, once=False):
        self.record("snapshots", collector="start", kernel=os.uname().release,
                    network_namespace=os.readlink("/proc/self/ns/net"))
        threads = []
        if not once:
            streams = [
                ("network-events", ["ip", "-ts", "monitor", "link", "address", "route", "rule"]),
                ("system-events", [
                    "journalctl", "-b", "-f", "-n", "100", "--no-pager", "-o", "short-iso-precise",
                    "_TRANSPORT=kernel", "+", "SYSLOG_IDENTIFIER=systemd", "+",
                    "_SYSTEMD_UNIT=NetworkManager.service", "+",
                    "_SYSTEMD_UNIT=systemd-networkd.service",
                ]),
            ]
            for stream, command in streams:
                worker = threading.Thread(target=self.follow, args=(stream, command), daemon=True)
                worker.start()
                threads.append(worker)
        try:
            self.snapshot("startup")
            deadline, earliest_event = time.monotonic() + self.interval, time.monotonic() + 5
            while not once and not self.stop.wait(1):
                now = time.monotonic()
                changed = self.network_changed.is_set() and now >= earliest_event
                if changed or now >= deadline:
                    self.network_changed.clear()
                    self.snapshot("network-change" if changed else "periodic")
                    deadline, earliest_event = time.monotonic() + self.interval, time.monotonic() + 5
        finally:
            self.stop.set()
            for worker in threads:
                worker.join(timeout=5)
            self.record("snapshots", collector="stop")
            for logger in self.loggers.values():
                for handler in logger.handlers:
                    handler.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=Path("/var/log/robot-network-evidence"))
    parser.add_argument("--interval", type=int, default=30, help="Snapshot interval, seconds (minimum 5)")
    parser.add_argument("--max-log-mib", type=int, default=10, help="Size per log segment, MiB")
    parser.add_argument("--backups", type=int, default=5, help="Rotated files retained per stream")
    parser.add_argument("--once", action="store_true", help="Write one snapshot and exit")
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("Run this collector on the Linux industrial PC, outside containers.")
    if args.interval < 5 or args.max_log_mib < 1 or args.backups < 1:
        parser.error("interval >= 5, max-log-mib >= 1 and backups >= 1 are required")
    for tool in ("ip", "ss", "ps", "systemctl", "journalctl"):
        if shutil.which(tool) is None:
            parser.error("Required tool not found: " + tool)
    if os.geteuid() != 0:
        parser.error("Run with sudo to capture service ownership and kernel logs.")
    import fcntl
    os.umask(0o077)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with (args.output_dir / ".collector.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("Another collector is already using this output directory.")
        collector = Collector(args.output_dir, args.interval, args.max_log_mib * 1024 * 1024, args.backups)
        for signum in (signal.SIGINT, signal.SIGTERM):
            signal.signal(signum, lambda *_: collector.stop.set())
        print("Collecting evidence in {} (Ctrl+C stops collection)".format(args.output_dir), flush=True)
        collector.run(once=args.once)
    return 0


if __name__ == "__main__":
    sys.exit(main())
