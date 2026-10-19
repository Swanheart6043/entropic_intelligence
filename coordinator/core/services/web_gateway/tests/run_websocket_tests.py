#!/usr/bin/env python3
"""Local TCP fault injection. The fake cloud never acknowledges event reports."""
import base64
import hashlib
import socket
import struct
import subprocess
import sys
import threading
import time


def exact(sock, count):
    data = b""
    while len(data) < count:
        part = sock.recv(count - len(data))
        if not part:
            raise EOFError
        data += part
    return data


def frame(sock):
    header = exact(sock, 2)
    size = header[1] & 127
    if size == 126:
        size = struct.unpack("!H", exact(sock, 2))[0]
    elif size == 127:
        size = struct.unpack("!Q", exact(sock, 8))[0]
    mask = exact(sock, 4) if header[1] & 128 else b"\0" * 4
    data = exact(sock, size)
    return header[0] & 15, bytes(v ^ mask[i % 4] for i, v in enumerate(data))


def run(executable, mode):
    received = []
    errors = []
    stop = threading.Event()
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    listener.settimeout(0.2)
    workers = []

    def handle(sock, number):
        with sock:
            try:
                sock.settimeout(5)
                if mode in ("blackhole", "reset") and number == 1:
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
                request = b""
                while b"\r\n\r\n" not in request:
                    request += exact(sock, 1)
                if mode == "retry" and number < 3:
                    sock.sendall(b"HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n")
                    return
                key = next(line.split(b":", 1)[1].strip() for line in request.split(b"\r\n")
                           if line.lower().startswith(b"sec-websocket-key:"))
                accept = base64.b64encode(hashlib.sha1(key + b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest())
                sock.sendall(b"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                             b"Connection: Upgrade\r\nSec-WebSocket-Accept: " + accept + b"\r\n\r\n")
                if mode == "blackhole" and number == 1:
                    stop.wait(5)  # Router black hole: peer remains open but stops reading.
                    return
                if mode == "reset" and number == 1:
                    time.sleep(0.05)
                    linger = struct.pack("HH" if sys.platform == "win32" else "ii", 1, 0)
                    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, linger)
                    return
                if mode in ("normal", "retry"):
                    sock.sendall(b"\x81\x08downlink")
                while not stop.is_set():
                    opcode, message = frame(sock)
                    if opcode == 8:
                        return
                    if opcode == 1:
                        received.append(message)
                    # No cloud business ACK, even after receiving the event.
            except (EOFError, ConnectionError, TimeoutError, OSError):
                pass
            except Exception as exc:
                errors.append(exc)

    def serve():
        count = 0
        while not stop.is_set():
            try:
                sock, _ = listener.accept()
            except socket.timeout:
                continue
            count += 1
            worker = threading.Thread(target=handle, args=(sock, count), daemon=True)
            workers.append(worker)
            worker.start()

    server = threading.Thread(target=serve, daemon=True)
    server.start()
    try:
        result = subprocess.run([executable, str(listener.getsockname()[1]), mode],
                                capture_output=True, timeout=25)
        output = result.stdout.decode("utf-8", errors="replace") + result.stderr.decode("utf-8", errors="replace")
        if result.returncode or errors:
            raise AssertionError(output + repr(errors))
        assert b"event-without-cloud-ack" in received, output
        if mode in ("normal", "retry"):
            assert b"callback-reply" in received, output
        if mode == "blackhole":
            assert "本地写入超时" in output, output
        assert "[info]" in output and "WebSocket发送云端失败" in output, output
        print("PASS:", mode, "(received event; no business ACK)")
    finally:
        stop.set()
        server.join(1)
        listener.close()
        for worker in workers:
            worker.join(1)


if __name__ == "__main__":
    for scenario in ("normal", "retry", "blackhole", "reset"):
        run(sys.argv[1], scenario)
