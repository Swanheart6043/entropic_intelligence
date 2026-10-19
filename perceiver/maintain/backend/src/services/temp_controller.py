import configparser
import struct
import threading
from pathlib import Path
from typing import Any, Dict, Optional
from loguru import logger
try:
    import serial
except ImportError: # 缺依赖时服务仍可导入, 调用时报错
    serial = None

CONFIG_PATH = Path("/etc/patrol_robot_h1/config.ini")
CONFIG_SECTION = "temp_controller"
DEFAULT_PORT = "/dev/ttyUSBrelay"
DEFAULT_BAUDRATE = 9600
DEFAULT_SLAVE = 1
DEFAULT_TIMEOUT = 0.5 # 实测应答延迟 ~110ms, 0.5s 足够

# 可读写参数: 名称 -> (寄存器, 系数, 是否有符号)
PARAM_REGS = {
    "temp_unit": (0x02, 1, False), # 0=摄氏C, 1=华氏F
    "filter": (0x05, 1, False), # 滤波系数 0~10
    "compensation": (0x06, 0.1, True), # 温度补偿 -20~20
    "power_on_delay": (0x07, 1, False), # 上电延时 0~999
    "relay1_type": (0x08, 1, False), # 0=H上限 1=L下限 2=HL 3=FRO 4=CYS
    "relay1_setpoint": (0x09, 0.1, True), # 动作值
    "relay1_hysteresis": (0x0A, 0.1, True), # 回差值
    "relay1_on_delay": (0x0B, 1, False), # 动作延时
    "relay1_off_delay": (0x0C, 1, False), # 解除延时
    "relay2_type": (0x0D, 1, False),
    "relay2_setpoint": (0x0E, 0.1, True),
    "relay2_hysteresis": (0x0F, 0.1, True),
    "relay2_on_delay": (0x10, 1, False),
    "relay2_off_delay": (0x11, 1, False),
}

REG_TEMP = 0x16         # 当前温度 (R, ×10 有符号)
REG_RELAY1_STATE = 0x17 # 继电器1状态 (R, 0=释放 1=吸合)
REG_RELAY2_STATE = 0x18 # 继电器2状态 (R)
REG_RELAY1_FORCE = 0x13 # 继电器1强制控制 (0=正常 1=强制关 2=强制开)
REG_RELAY2_FORCE = 0x14

def _crc16(data: bytes) -> bytes:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return struct.pack("<H", crc)

def _check_crc(frame: bytes) -> bool:
    return len(frame) >= 4 and _crc16(frame[:-2]) == frame[-2:]

def _to_raw(value: float, scale: float, signed: bool) -> int:
    raw = int(round(value / scale))
    return raw & 0xFFFF if signed and raw < 0 else raw

def _from_raw(raw: int, scale: float, signed: bool) -> float:
    if signed and raw >= 0x8000:
        raw -= 0x10000
    return raw * scale

class TempControllerService:
    def __init__(self, config_path: Path = CONFIG_PATH):
        self._lock = threading.Lock()  # 串口事务互斥: HTTP 并发请求不能交错收发
        self._ser = None
        self.port, self.baudrate, self.slave, self.timeout = self._load_conn_config(config_path)
        logger.info(
            f"Temp controller service: port={self.port} baud={self.baudrate} slave={self.slave}"
        )

    @staticmethod
    def _load_conn_config(config_path: Path):
        port, baud, slave, timeout = DEFAULT_PORT, DEFAULT_BAUDRATE, DEFAULT_SLAVE, DEFAULT_TIMEOUT
        try:
            if config_path.exists():
                parser = configparser.ConfigParser()
                parser.read(config_path, encoding="utf-8")
                if parser.has_section(CONFIG_SECTION):
                    port = parser.get(CONFIG_SECTION, "port", fallback=port)
                    baud = parser.getint(CONFIG_SECTION, "baudrate", fallback=baud)
                    slave = parser.getint(CONFIG_SECTION, "slave", fallback=slave)
                    timeout = parser.getfloat(CONFIG_SECTION, "timeout", fallback=timeout)
        except Exception as e:
            logger.warning(f"Read [{CONFIG_SECTION}] from {config_path} failed, use defaults: {e}")
        return port, baud, slave, timeout

    # ------------------------------------------------------------------ 串口层
    def _ensure_open(self):
        if serial is None:
            raise RuntimeError("pyserial 未安装 (pip install pyserial)")
        if self._ser is None or not self._ser.is_open:
            self._ser = serial.Serial(
                port=self.port, baudrate=self.baudrate,
                bytesize=8, parity="N", stopbits=1, timeout=self.timeout,
            )
            logger.info(f"Opened {self.port} @ {self.baudrate}bps")

    def _drop_port(self):
        try:
            if self._ser:
                self._ser.close()
        except Exception:
            pass
        self._ser = None

    def _transact(self, frame: bytes, func: int, retries: int = 1) -> bytes:
        """一次事务: 发送并按协议定长收帧. 调用方必须已持有 self._lock"""
        last = b""
        for attempt in range(retries + 1):
            self._ser.reset_input_buffer()
            self._ser.write(frame)
            self._ser.flush()
            if func == 0x06:
                rx = self._ser.read(8)  # 写命令: 固定8字节回显
            else:
                head = self._ser.read(3)
                if len(head) < 3:
                    rx = head
                elif head[1] & 0x80:  # MODBUS 异常应答
                    rx = head + self._ser.read(2)
                else:
                    rx = head + self._ser.read(head[2] + 2)
            if rx and _check_crc(rx):
                return rx
            last = rx
            logger.warning(f"transact retry {attempt + 1}: tx={frame.hex(' ')} rx={rx.hex(' ')}")
        return last

    # ---------------------------------------------------------------- 协议层
    def _read_reg(self, reg: int) -> Optional[int]:
        """读单个寄存器原始值, 失败返回 None"""
        body = struct.pack(">BBHH", self.slave, 0x03, reg, 1)
        rx = self._transact(body + _crc16(body), 0x03)
        if len(rx) >= 7 and rx[0] == self.slave and rx[1] == 0x03 and rx[2] == 2:
            return (rx[3] << 8) | rx[4]
        return None

    def _write_reg(self, reg: int, raw: int) -> bool:
        body = struct.pack(">BBHH", self.slave, 0x06, reg, raw & 0xFFFF)
        frame = body + _crc16(body)
        return self._transact(frame, 0x06) == frame  # 设备原样回显即成功

    # ---------------------------------------------------------------- 业务接口
    def read_status(self) -> Dict:
        """温度 + 单位 + 继电器状态"""
        try:
            with self._lock:
                self._ensure_open()
                temp_raw = self._read_reg(REG_TEMP)
                unit_raw = self._read_reg(PARAM_REGS["temp_unit"][0])
                r1 = self._read_reg(REG_RELAY1_STATE)
                r2 = self._read_reg(REG_RELAY2_STATE)
        except Exception as e:
            self._drop_port()
            logger.error(f"read_status failed: {e}")
            return {"success": False, "message": f"温控仪通讯失败: {e}"}
        if temp_raw is None:
            return {"success": False, "message": "温控仪无应答 (检查接线/地址/波特率)"}
        return {
            "success": True,
            "data": {
                "temperature": _from_raw(temp_raw, 0.1, True),
                "unit": "F" if unit_raw == 1 else "C",
                "relay1_engaged": r1 == 1,
                "relay2_engaged": r2 == 1,
            },
        }

    def read_params(self) -> Dict:
        """读取全部可编辑参数 (逐寄存器读, 约 14×150ms≈2s)"""
        try:
            with self._lock:
                self._ensure_open()
                data, failed = {}, []
                for name, (reg, scale, signed) in PARAM_REGS.items():
                    raw = self._read_reg(reg)
                    if raw is None:
                        failed.append(name)
                    else:
                        val = _from_raw(raw, scale, signed)
                        data[name] = round(val, 1) if scale != 1 else int(val)
        except Exception as e:
            self._drop_port()
            logger.error(f"read_params failed: {e}")
            return {"success": False, "message": f"温控仪通讯失败: {e}"}
        if failed and not data:
            return {"success": False, "message": "温控仪无应答"}
        result = {"success": True, "data": data}
        if failed:
            result["message"] = f"部分寄存器读取失败: {failed}"
        return result

    def write_params(self, params: Dict[str, Any]) -> Dict:
        """批量写入参数, 逐项写入并读回校验, 风格同 ConfigManager.update_config"""
        updated, failed, skipped = [], [], []
        try:
            with self._lock:
                self._ensure_open()
                for name, value in params.items():
                    if name not in PARAM_REGS:
                        skipped.append({"key": name, "reason": "未知参数"})
                        continue
                    if value is None:
                        continue
                    reg, scale, signed = PARAM_REGS[name]
                    raw = _to_raw(float(value), scale, signed)
                    if not self._write_reg(reg, raw):
                        failed.append({"key": name, "message": "写入无回显"})
                        continue
                    back = self._read_reg(reg)  # 读回校验
                    if back != raw:
                        failed.append({"key": name, "message": f"读回不一致: 写{raw} 回{back}"})
                        continue
                    updated.append({"key": name, "new": value})
        except Exception as e:
            self._drop_port()
            logger.error(f"write_params failed: {e}")
            return {"success": False, "message": f"温控仪通讯失败: {e}"}

        if failed:
            logger.error(f"write_params partial failure: updated={updated}, failed={failed}")
            return {
                "success": False,
                "message": f"部分参数写入失败 (成功 {len(updated)}, 失败 {len(failed)})",
                "updated": updated, "failed": failed, "skipped": skipped,
            }
        logger.info(f"Temp controller params updated: {updated}")
        return {
            "success": True,
            "message": f"已写入 {len(updated)} 项参数",
            "updated": updated, "skipped": skipped,
        }

    def force_relay(self, relay: int, mode: int) -> Dict:
        """继电器强制控制: relay=1/2, mode 0=正常 1=强制关闭 2=强制开启.
        注意: 强制模式下面板数码管显示 '---', 用完务必写回 0"""
        if relay not in (1, 2) or mode not in (0, 1, 2):
            return {"success": False, "message": "relay 须为 1/2, mode 须为 0/1/2"}
        reg = REG_RELAY1_FORCE if relay == 1 else REG_RELAY2_FORCE
        try:
            with self._lock:
                self._ensure_open()
                ok = self._write_reg(reg, mode)
        except Exception as e:
            self._drop_port()
            logger.error(f"force_relay failed: {e}")
            return {"success": False, "message": f"温控仪通讯失败: {e}"}
        if not ok:
            return {"success": False, "message": "写入无回显"}
        mode_name = {0: "正常", 1: "强制关闭", 2: "强制开启"}[mode]
        logger.info(f"Relay{relay} force mode -> {mode_name}")
        return {"success": True, "message": f"继电器{relay} 已切换为 {mode_name}"}

_temp_controller: Optional[TempControllerService] = None

def get_temp_controller() -> TempControllerService:
    global _temp_controller
    if _temp_controller is None:
        _temp_controller = TempControllerService()
    return _temp_controller