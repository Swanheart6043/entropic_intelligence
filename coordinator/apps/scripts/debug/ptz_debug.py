#!/usr/bin/env python3
"""
Pelco-D 串口调试器 — 对照 PTZController 的协议实现

帧结构: FF addr cmd1 cmd2 data1 data2 checksum
校验和: sum(bytes[1..5]) & 0xFF   (与 C++ 的 checksum5 一致)

用法:
    python3 ptz_debug.py                       # 交互模式 (默认 /dev/ttyS0 @ 9600)
    # 仅依赖 Python 标准库 (termios), 无需 pyserial, 内网可直接运行
    python3 ptz_debug.py -p /dev/ttyS1 -b 9600 # 指定端口/波特率
    python3 ptz_debug.py --once up             # 单次发一个命令后退出
    python3 ptz_debug.py --once "FF 01 00 08 00 1E"  # 单次发一帧裸 hex

交互模式命令:
    up / down / left / right / stop      方向 (发 0.5s 后自动 stop, 跟 C++ 行为一致)
    up-raw / down-raw / ...              只发方向, 不自动 stop
    zoom-in / zoom-out                   (通过 cmd2=0x09/0x0B? 见下注)
    wiper-on / wiper-off                 雨刷
    pan <deg>                            绝对水平角 0..359.99   (cmd2=0x4B)
    tilt <deg>                           绝对俯仰角            (cmd2=0x4D)
    qpan                                 查询水平角 (期待响应 cmd2=0x59)
    qtilt                                查询俯仰角 (期待响应 cmd2=0x5B)
    hex FF 01 00 08 00 1E                发任意帧; 末尾校验和缺省自动补全
    cmd <c1> <c2> <d1> <d2>              用 4 个字节构帧 (hex), 自动算校验
    addr <n>                             改默认地址 (默认 1)
    monitor [秒]                         纯监听, 把收到的字节按 7 字节帧解析打印
    speed                                持续发方向并实时打印, Ctrl-C 停
    q / quit / exit                      退出
"""

import argparse
import os
import select
import sys
import termios
import time

# 仅用标准库 termios 配置串口, 无需 pyserial。
# 配置流程对照 C++ PTZController::open_serial。
BAUD_MAP = {
    1200: termios.B1200, 2400: termios.B2400, 4800: termios.B4800,
    9600: termios.B9600, 19200: termios.B19200, 38400: termios.B38400,
    57600: termios.B57600, 115200: termios.B115200,
}


# ----------------------- 协议层 -----------------------
def checksum5(frame):
    """校验和 = byte[1..5] 之和 & 0xFF, 与 C++ PTZController::checksum5 一致。"""
    return sum(frame[1:6]) & 0xFF


def build_frame(addr, cmd1, cmd2, data1, data2):
    f = [0xFF, addr & 0xFF, cmd1 & 0xFF, cmd2 & 0xFF, data1 & 0xFF, data2 & 0xFF, 0x00]
    f[6] = checksum5(f)
    return bytes(f)


def fmt(frame):
    return " ".join(f"{b:02X}" for b in frame)


# 命令字对照 C++ build_base_frame 调用 (cmd1, cmd2, data1, data2)
COMMANDS = {
    "up":      (0x00, 0x08, 0x00, 0x1E),
    "down":    (0x00, 0x10, 0x00, 0x1E),
    "left":    (0x00, 0x04, 0x20, 0x00),
    "right":   (0x00, 0x02, 0x20, 0x00),
    "stop":    (0x00, 0x00, 0x00, 0x00),
    "wiper-on":  (0x00, 0x09, 0x00, 0x01),
    "wiper-off": (0x00, 0x0B, 0x00, 0x01),
}


# ----------------------- 串口层 -----------------------
class PtzPort:
    def __init__(self, port, baud, addr=0x01):
        if baud not in BAUD_MAP:
            sys.exit(f"不支持的波特率 {baud}, 可选: {sorted(BAUD_MAP)}")
        # 对照 C++: open(O_RDWR | O_NOCTTY | O_SYNC)
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_SYNC)

        # attr = [iflag, oflag, cflag, lflag, ispeed, ospeed, cc]
        attr = termios.tcgetattr(self.fd)
        iflag, oflag, cflag, lflag, _, _, cc = attr

        # cfmakeraw 等价: 清掉所有规范化/回显/流控
        iflag &= ~(termios.IGNBRK | termios.BRKINT | termios.PARMRK |
                   termios.ISTRIP | termios.INLCR | termios.IGNCR |
                   termios.ICRNL | termios.IXON)
        oflag &= ~termios.OPOST
        lflag &= ~(termios.ECHO | termios.ECHONL | termios.ICANON |
                   termios.ISIG | termios.IEXTEN)

        # 8N1 + CREAD | CLOCAL, 对照 C++ 的 cflag 设置
        cflag &= ~termios.PARENB   # no parity
        cflag &= ~termios.CSTOPB   # 1 stop bit
        cflag &= ~termios.CSIZE
        cflag |= termios.CS8       # 8 bits
        cflag |= (termios.CREAD | termios.CLOCAL)

        # 非规范读: VMIN=0, VTIME=5 (0.5s), 与 C++ 一致
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 5

        speed = BAUD_MAP[baud]
        termios.tcsetattr(self.fd, termios.TCSANOW,
                          [iflag, oflag, cflag, lflag, speed, speed, cc])
        termios.tcflush(self.fd, termios.TCIOFLUSH)

        self.addr = addr
        print(f"[打开] {port} @ {baud} 8N1, addr={addr}")

    def send(self, frame):
        os.write(self.fd, frame)
        termios.tcdrain(self.fd)  # 等价 C++ tcdrain, 确保字节发出去
        print(f"  -> TX: {fmt(frame)}")

    def send_cmd(self, cmd1, cmd2, data1, data2):
        f = build_frame(self.addr, cmd1, cmd2, data1, data2)
        self.send(f)
        return f

    def _read_byte(self, deadline):
        """在 deadline 前读 1 字节, 用 select 做超时, 对照 C++ 的 select 循环。"""
        remaining = deadline - time.time()
        if remaining <= 0:
            return None
        r, _, _ = select.select([self.fd], [], [], remaining)
        if not r:
            return None
        b = os.read(self.fd, 1)
        return b if b else None

    def read_frame(self, timeout_s=1.0):
        """读取一个 7 字节帧: 先找 0xFF 同步头, 再读 6 字节, 校验。
        逻辑对照 C++ PTZController::read_frame。"""
        deadline = time.time() + timeout_s
        while True:
            b = self._read_byte(deadline)
            if b is None:
                break
            if b[0] != 0xFF:
                continue  # 丢弃, 继续找同步头
            rest = bytearray()
            while len(rest) < 6:
                nb = self._read_byte(deadline)
                if nb is None:
                    break
                rest += nb
            if len(rest) < 6:
                break
            frame = bytes([0xFF]) + bytes(rest)
            if checksum5(frame) != frame[6]:
                print(f"  <- RX 校验失败: {fmt(frame)} (期望 {checksum5(frame):02X})")
                continue
            print(f"  <- RX: {fmt(frame)}")
            return frame
        print("  <- RX: 超时, 无有效帧")
        return None

    def query(self, cmd2_req, cmd2_resp):
        """发查询帧, 解析角度。pan: 0x51->0x59, tilt: 0x53->0x5B。"""
        self.send_cmd(0x00, cmd2_req, 0x00, 0x00)
        r = self.read_frame(1.0)
        if not r:
            return None
        if r[3] != cmd2_resp:
            print(f"  响应 cmd2={r[3]:02X} 不匹配期望 {cmd2_resp:02X}")
            return None
        raw = (r[4] << 8) | r[5]
        if cmd2_resp == 0x59:  # pan
            angle = raw / 100.0
        else:  # tilt, 对照 C++ query_tilt 的换算
            angle = (36000 - raw) / 100.0 if raw > 18000 else -raw / 100.0
        print(f"  角度 = {angle:.2f}° (raw={raw})")
        return angle

    def monitor(self, seconds):
        print(f"[监听] {seconds}s, Ctrl-C 提前结束 ...")
        end = time.time() + seconds
        try:
            while time.time() < end:
                self.read_frame(0.5)
        except KeyboardInterrupt:
            print("\n[监听结束]")

    def close(self):
        os.close(self.fd)
        print("[关闭]")


# ----------------------- 命令解析 -----------------------
def parse_hex_bytes(tokens):
    return [int(t, 16) for t in tokens]


def dispatch(port, line):
    parts = line.split()
    if not parts:
        return True
    cmd = parts[0].lower()

    if cmd in ("q", "quit", "exit"):
        return False

    # 方向: 默认发后 0.5s 自动 stop (对照 C++ to_up 等)
    if cmd in ("up", "down", "left", "right"):
        c1, c2, d1, d2 = COMMANDS[cmd]
        port.send_cmd(c1, c2, d1, d2)
        time.sleep(0.5)
        port.send_cmd(*COMMANDS["stop"])
        return True

    # 方向 raw: 不自动 stop, 自己手动 stop
    if cmd.endswith("-raw") and cmd[:-4] in COMMANDS:
        port.send_cmd(*COMMANDS[cmd[:-4]])
        return True

    if cmd in ("stop", "wiper-on", "wiper-off"):
        port.send_cmd(*COMMANDS[cmd])
        return True

    if cmd == "pan" and len(parts) == 2:
        deg = float(parts[1]) % 360.0
        val = int(deg * 100 + 0.5)
        port.send_cmd(0x00, 0x4B, (val >> 8) & 0xFF, val & 0xFF)
        return True

    if cmd == "tilt" and len(parts) == 2:
        deg = float(parts[1])
        val = min(int(deg * 100 + 0.5), 35999)
        port.send_cmd(0x00, 0x4D, (val >> 8) & 0xFF, val & 0xFF)
        return True

    if cmd == "qpan":
        port.query(0x51, 0x59)
        return True

    if cmd == "qtilt":
        port.query(0x53, 0x5B)
        return True

    if cmd == "hex":
        try:
            raw = parse_hex_bytes(parts[1:])
            if len(raw) == 6:        # 缺校验和, 自动补
                raw = raw + [0]
                raw[6] = checksum5(raw)
            elif len(raw) == 7:      # 完整帧, 重算校验提示
                if checksum5(raw) != raw[6]:
                    print(f"  注意: 你给的校验 {raw[6]:02X} != 计算值 {checksum5(raw):02X}")
            else:
                print("  hex 需要 6 (自动补校验) 或 7 字节")
                return True
            port.send(bytes(raw))
        except ValueError:
            print("  hex 解析失败, 例: hex FF 01 00 08 00 1E")
        return True

    if cmd == "cmd" and len(parts) == 5:
        try:
            c1, c2, d1, d2 = parse_hex_bytes(parts[1:5])
            port.send_cmd(c1, c2, d1, d2)
        except ValueError:
            print("  例: cmd 00 08 00 1E")
        return True

    if cmd == "addr" and len(parts) == 2:
        port.addr = int(parts[1])
        print(f"  默认地址 -> {port.addr}")
        return True

    if cmd == "monitor":
        secs = int(parts[1]) if len(parts) > 1 else 10
        port.monitor(secs)
        return True

    print(f"  未知命令: {line}  (输入 help 查看)")
    return True


HELP = __doc__.split("交互模式命令:")[1]


def interactive(port):
    print("输入 help 看命令, q 退出。")
    while True:
        try:
            line = input("ptz> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if line.lower() == "help":
            print(HELP)
            continue
        if not dispatch(port, line):
            break


def main():
    ap = argparse.ArgumentParser(description="Pelco-D 串口调试器")
    ap.add_argument("-p", "--port", default="/dev/ttyS0")
    ap.add_argument("-b", "--baud", type=int, default=9600)
    ap.add_argument("-a", "--addr", type=lambda x: int(x, 0), default=0x01)
    ap.add_argument("--once", help="单次执行一条命令后退出")
    args = ap.parse_args()

    port = PtzPort(args.port, args.baud, args.addr)
    try:
        if args.once:
            dispatch(port, args.once)
        else:
            interactive(port)
    finally:
        port.close()


if __name__ == "__main__":
    main()