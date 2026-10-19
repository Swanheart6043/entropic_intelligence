#!/usr/bin/env python3
"""
CommandReceiver - ZMQ REP 命令接收器

命令域传输组件: 后台线程监听 REQ, 按 cmd 字段分发到注册的 handler, 返回 REP
handler 返回 dict 自动包装为标准应答格式
"""
import json
import threading
import time
import logging
import traceback
import zmq

logger = logging.getLogger("CommandReceiver")

class CommandReceiver:
    def __init__(self, endpoint: str = "tcp://*:2430", recv_timeout: int = 1000):
        self.endpoint = endpoint
        self.recv_timeout = recv_timeout
        self._handlers: dict[str, callable] = {}
        self._running = False
        self._thread: threading.Thread | None = None
        self._stats_count = 0

    def register(self, cmd: str, handler: callable):
        """注册命令处理函数"""
        self._handlers[cmd] = handler
        logger.debug(f"registered handler: {cmd}")

    def start(self):
        if self._running:
            return
        self._running = True
        self._thread = threading.Thread(target=self._recv_loop, daemon=True, name="CommandReceiver")
        self._thread.start()
        cmds = list(self._handlers.keys())
        logger.info(f"started on {self.endpoint}, commands: {cmds}")

    def stop(self):
        self._running = False
        if self._thread:
            self._thread.join(timeout=5)
            self._thread = None
        logger.info(f"stopped, handled {self._stats_count} commands")

    @property
    def is_running(self) -> bool:
        return self._running

    def _kill_port_holder(self):
        """尝试杀掉占用端口的旧进程"""
        import subprocess
        import os
        import re
        # 从 endpoint 提取端口号
        m = re.search(r':(\d+)$', self.endpoint)
        if not m:
            return
        port = m.group(1)
        try:
            result = subprocess.run(
                ['fuser', f'{port}/tcp'],
                capture_output=True, text=True, timeout=3
            )
            pids = result.stdout.strip().split()
            my_pid = str(os.getpid())
            for pid in pids:
                pid = pid.strip()
                if pid and pid != my_pid:
                    logger.warning(f"杀掉占用端口 {port} 的进程: PID={pid}")
                    os.kill(int(pid), 9)
        except Exception as e:
            logger.debug(f"fuser 检查端口失败: {e}")

    def _recv_loop(self):
        ctx = zmq.Context()
        sock = ctx.socket(zmq.REP)
        sock.setsockopt(zmq.RCVTIMEO, self.recv_timeout)
        sock.setsockopt(zmq.LINGER, 0)

        try:
            sock.bind(self.endpoint)
        except zmq.ZMQError as e:
            logger.warning(f"bind 失败: {self.endpoint} - {e}, 尝试清理旧连接...")
            # 关闭当前 socket 重建
            sock.close()
            ctx.term()
            # 如果是 tcp 端口被占用，尝试杀掉占用进程
            self._kill_port_holder()
            import time as _time
            _time.sleep(2.0)
            # 重新创建
            ctx = zmq.Context()
            sock = ctx.socket(zmq.REP)
            sock.setsockopt(zmq.RCVTIMEO, self.recv_timeout)
            sock.setsockopt(zmq.LINGER, 0)
            try:
                sock.bind(self.endpoint)
                logger.info(f"重新 bind 成功: {self.endpoint}")
            except zmq.ZMQError as e2:
                logger.error(f"bind 仍然失败: {self.endpoint} - {e2}")
                sock.close()
                ctx.term()
                self._running = False
                return

        while self._running:
            try:
                raw = sock.recv()
            except zmq.Again:
                continue
            except zmq.ZMQError as e:
                logger.error(f"recv error: {e}")
                time.sleep(0.1)
                continue

            reply = self._dispatch(raw)

            try:
                sock.send_string(json.dumps(reply, ensure_ascii=False))
            except zmq.ZMQError as e:
                logger.error(f"send error: {e}")

        sock.close()
        ctx.term()

    def _dispatch(self, raw: bytes) -> dict:
        """解析消息, 查找 handler, 执行并构造应答"""
        self._stats_count += 1

        # 解析 JSON
        try:
            msg = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            logger.warning(f"invalid JSON: {e}")
            return self._error_reply("unknown", None, "invalid_json", str(e))

        cmd = msg.get("cmd", "")
        request_id = msg.get("request_id")
        logger.info(f"收到命令: cmd={cmd}, keys={list(msg.keys())}")

        # 查找 handler
        handler = self._handlers.get(cmd)
        if handler is None:
            logger.warning(f"unknown cmd: {cmd}")
            return self._error_reply(
				cmd, request_id, 
				"unknown_command",
            	f"unknown cmd: {cmd}"
			)

        # 执行 handler
        try:
            t0 = time.time()
            result = handler(msg)
            elapsed_ms = (time.time() - t0) * 1000
            logger.info(f"[{cmd}] ok, {elapsed_ms:.0f}ms")

            # handler 返回 error
            if isinstance(result, dict) and "error" in result:
                return self._error_reply(
					cmd, request_id,
					result["error"],
                    result.get("message", ""))
            return {
                "cmd": cmd,
                "request_id": request_id,
                "ok": True,
                "data": result if isinstance(result, dict) else {},
            }

        except Exception as e:
            logger.error(f"[{cmd}] handler error: {e}")
            logger.debug(traceback.format_exc())
            return self._error_reply(cmd, request_id, "internal_error", str(e))

    @staticmethod
    def _error_reply(cmd: str, request_id, error: str, message: str = "") -> dict:
        return {
            "cmd": cmd,
            "request_id": request_id,
            "ok": False,
            "error": error,
            "message": message,
        }
