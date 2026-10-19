#!/usr/bin/env python3
"""
HWDecodeCapture - RDK X5 硬件解码 RTSP 采集
设计:
  - OpenCV 只做 RTSP demux (CAP_PROP_FORMAT=-1, 不软解码, CPU≈0)
  - hobot_vio Decoder 硬件解码 H264/H265 → NV12 (CPU≈0)
  - 后台线程持续拉流解码, 通过 Queue 向外提供帧
  - 断流自动重连 (指数退避, 解码器卡死检测)
"""
import cv2
import logging
import numpy as np
import threading
from queue import Queue, Empty
from time import sleep
try:
    from hobot_vio import libsrcampy as srcampy
except ImportError:
    srcampy = None

logger = logging.getLogger("HWDecode")

class HWDecodeCapture:
    """
    RDK X5 硬件解码 RTSP 采集器

    用法:
        cap = HWDecodeCapture("rtsp://admin:pass@10.10.10.40:554/stream")
        ret, nv12 = cap.read()         # 返回 NV12 bytes
        ret, bgr  = cap.read_bgr()     # 返回 BGR numpy array
        cap.release()
    """

    def __init__(self, rtsp_url: str, dec_chn: int = 0):
        if srcampy is None:
            raise RuntimeError("hobot_vio 未安装, 无法使用硬件解码")

        self.rtsp_url = rtsp_url
        self.dec_chn = dec_chn
        self.width = 0
        self.height = 0
        self.cap = None
        self.dec = None

        # 内部状态
        self._find_pps_sps = False
        self._skip_count = 0
        self._running = True
        self._queue = Queue(maxsize=2)

        # 首次打开
        self._open()

        # 启动后台线程
        self._thread = threading.Thread(
            target=self._loop, daemon=True, name="HWDecode-loop"
        )
        self._thread.start()

    # ----------------------------------------------------------
    #  打开 / 关闭
    # ----------------------------------------------------------
    def _open(self):
        """打开 RTSP 流 + 初始化硬件解码器"""
        self.cap = cv2.VideoCapture(self.rtsp_url)
        self.cap.set(cv2.CAP_PROP_FORMAT, -1)  # 拿编码裸流, 不软解码

        if not self.cap.isOpened():
            raise RuntimeError(f"RTSP 打开失败: {self.rtsp_url}")

        self.width = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        self.height = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        dec_type = self._detect_codec()

        self.dec = srcampy.Decoder()
        ret = self.dec.decode("", self.dec_chn, dec_type, self.width, self.height)
        logger.info(
            f"opened: {self.width}x{self.height}, "
            f"codec={'H264' if dec_type == 1 else 'H265'}, ret={ret}"
        )

        # 重置状态
        self._find_pps_sps = False
        self._skip_count = 0

    def _close(self):
        """安全关闭解码器和采集器"""
        try:
            if self.dec:
                self.dec.close()
        except Exception:
            pass
        try:
            if self.cap:
                self.cap.release()
        except Exception:
            pass
        self.dec = None
        self.cap = None

    def _detect_codec(self) -> int:
        """
        检测编码类型
        返回: 1=H264, 2=H265
        """
        fourcc = int(self.cap.get(cv2.CAP_PROP_FOURCC))
        try:
            tag = bytes([
                fourcc & 0xFF,
                (fourcc >> 8) & 0xFF,
                (fourcc >> 16) & 0xFF,
                (fourcc >> 24) & 0xFF,
            ]).decode("ascii").lower()
        except Exception:
            tag = ""

        if "h264" in tag or "avc" in tag:
            return 1
        if "hevc" in tag or "h265" in tag:
            return 2
        # fallback: 默认 H264
        return 1

    # ----------------------------------------------------------
    #  SPS/PPS 检测
    # ----------------------------------------------------------
    @staticmethod
    def _check_sps_pps(data: bytes) -> bool:
        """检查是否包含 SPS/PPS (H264) 或 VPS (H265)"""
        # H264 SPS: NAL type 0x67 或 0x27
        # H265 VPS: NAL type 0x40
        return (
            b"\x00\x00\x00\x01\x67" in data
            or b"\x00\x00\x01\x67" in data
            or b"\x00\x00\x00\x01\x27" in data
            or b"\x00\x00\x01\x27" in data
            or b"\x00\x00\x00\x01\x40" in data
            or b"\x00\x00\x01\x40" in data
        )

    # ----------------------------------------------------------
    #  重连
    # ----------------------------------------------------------
    def _reconnect(self):
        """安全重连: 关闭旧资源 → 清空队列 → 退避重连"""
        # 清空队列, 避免下游拿到断流前的旧帧
        while not self._queue.empty():
            try:
                self._queue.get_nowait()
            except Empty:
                break

        self._close()

        # 指数退避重连
        for attempt in range(1, 100):
            if not self._running:
                return

            wait = min(attempt * 2, 30)  # 2s, 4s, 6s, ... 最多 30s
            logger.info(f"第 {attempt} 次重连, 等待 {wait}s...")
            sleep(wait)

            if not self._running:
                return

            try:
                self._open()
                logger.info("重连成功")
                return
            except Exception as e:
                logger.error(f"重连失败: {e}")

        logger.error("重连次数耗尽, 放弃")

    # ----------------------------------------------------------
    #  后台拉帧循环
    # ----------------------------------------------------------
    def _loop(self):
        """后台线程: demux + 硬件解码"""
        no_frame_count = 0
        MAX_NO_FRAME = 150  # 连续 ~5 秒拿不到帧就重连

        while self._running:
            try:
                # 1) 读取编码数据
                ret, stream = self.cap.read()
                if not ret:
                    logger.warning("cap.read() 失败, 准备重连...")
                    self._reconnect()
                    no_frame_count = 0
                    continue

                data = stream.tobytes()

                # 2) 等待 SPS/PPS (第一帧必须包含, 否则解码器崩溃)
                if not self._find_pps_sps:
                    if not self._check_sps_pps(data):
                        continue
                    self._find_pps_sps = True

                # 3) 喂数据给硬件解码器
                ret = self.dec.set_img(data, self.dec_chn)
                if ret != 0:
                    logger.error(f"set_img 返回 {ret}, 准备重连...")
                    self._reconnect()
                    no_frame_count = 0
                    continue

                # 4) 前几帧跳过, 等解码器稳定
                if self._skip_count < 8:
                    self._skip_count += 1
                    continue

                # 5) 获取解码后的 NV12 帧
                frame = self.dec.get_img()
                if frame is not None:
                    no_frame_count = 0
                    if not self._queue.full():
                        self._queue.put(frame)
                else:
                    no_frame_count += 1
                    if no_frame_count >= MAX_NO_FRAME:
                        logger.warning(
                            f"连续 {MAX_NO_FRAME} 次 get_img 为空, 解码器可能卡死, 重连..."
                        )
                        self._reconnect()
                        no_frame_count = 0

            except Exception as e:
                logger.error(f"循环异常: {e}", exc_info=True)
                self._reconnect()
                no_frame_count = 0

    # ----------------------------------------------------------
    #  对外接口
    # ----------------------------------------------------------
    def read(self):
        """
        读取一帧 NV12 数据

        返回: (ret: bool, nv12: bytes | None)
            nv12 可直接喂 BPU: model.forward(np.frombuffer(nv12, dtype=np.uint8))
        """
        if self._queue.empty():
            return False, None
        return True, self._queue.get()

    def read_bgr(self):
        """
        读取一帧 BGR numpy array

        返回: (ret: bool, bgr: np.ndarray | None)
            用于人脸/车牌等 CPU 推理
        """
        ret, nv12 = self.read()
        if not ret or nv12 is None:
            return False, None
        bgr = cv2.cvtColor(
            np.frombuffer(nv12, dtype=np.uint8).reshape(
                self.height * 3 // 2, self.width
            ),
            cv2.COLOR_YUV2BGR_NV12,
        )
        return True, bgr

    def release(self):
        """释放所有资源"""
        self._running = False
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=5)
        self._close()

        # 清空残留帧
        while not self._queue.empty():
            try:
                self._queue.get_nowait()
            except Empty:
                break

        logger.info("released")