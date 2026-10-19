import logging
import signal
import sys
import time
from common.config_provider import ConfigProvider
from common.cloud_api import CloudApi
from common.robot_api import RobotApi
from common import ZMQSender
from common.frame_hub import FrameHub
from command.command_controller import CommandController
from detection.detection_manager import DetectionManager
from monitor.monitoring_thread import MonitoringThread

logging.basicConfig(
    level=logging.INFO,
    format="[%(name)s] [%(asctime)s.%(msecs)03d] [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
logger = logging.getLogger("PatrolAI")

class PatrolAIService:
    def __init__(self):
        self.high_definition_hub: FrameHub | None = None
        self.infrared_thermal_hub: FrameHub | None = None
        self.zmq_sender: ZMQSender | None = None
        self.command_controller: CommandController | None = None
        self.detection_manager: DetectionManager | None = None
        self.monitoring_thread: MonitoringThread | None = None
        self._config_provider = ConfigProvider()
        config_snapshot = self._config_provider.get_all()
        # HTTP 相关配置按部署缓存；修改 config.json 后需重启进程生效。
        self._cloud_api = CloudApi.from_config(config_snapshot)
        self._robot_api = RobotApi.from_config(config_snapshot)

    def start(self) -> bool:
        logger.info("=" * 60)
        logger.info("主程序 启动中...")
        logger.info(f"启用模块: {self._config_provider.get_enable_detection()}")
        logger.info("=" * 60)

        # --- ZMQ告警发送端
        # 重要提示：ZeroMQ Socket非线程安全
        # 优化建议：业务线程内部自行创建ZMQSender，不要共用此实例
        self.zmq_sender = ZMQSender("tcp://10.10.10.10:2425")

        # --- 视频流初始化 ---
        high_definition_rtsp_url = "rtsp://admin:heyn_888@10.10.10.40:554/id=1&type=0"
        infrared_thermal_rtsp_url = "rtsp://admin:heyn_888@10.10.10.41:554/cam/realmonitor?channel=1&subtype=0"
        self.high_definition_hub = FrameHub(high_definition_rtsp_url)
        self.infrared_thermal_hub = FrameHub(infrared_thermal_rtsp_url, dec_chn=1)
        self.high_definition_hub.start()
        self.infrared_thermal_hub.start()

        # 等待高清首帧
        for _ in range(100):
            if self.high_definition_hub.get_frame() is not None:
                break
            time.sleep(0.1)
        else:
            logger.error("超时: 高清未获取到首帧, 退出")
            return False
        logger.info(f"高清首帧已获取: {self.high_definition_hub.width}x{self.high_definition_hub.height}")

        # 等待热成像首帧
        for _ in range(100):
            if self.infrared_thermal_hub.get_frame() is not None:
                break
            time.sleep(0.1)
        else:
            logger.error("超时: 热成像未获取到首帧, 退出")
            return False
        logger.info(f"热成像首帧已获取: {self.infrared_thermal_hub.width}x{self.infrared_thermal_hub.height}")

        # 检测管理模块
        enabled = self._config_provider.get_enable_detection()
        self.detection_manager = DetectionManager(
            high_definition_hub=self.high_definition_hub,
            infrared_thermal_hub=self.infrared_thermal_hub,
            zmq_sender=self.zmq_sender,
            config_provider=self._config_provider,
            cloud_api=self._cloud_api,
            robot_api=self._robot_api,
        )
        self.detection_manager.initialize(enabled)
        if not self.detection_manager.start_all():
            return False

        # 依赖和检测线程全部就绪后再开放命令端口，避免启动竞态。
        self.command_controller = CommandController(
            endpoint="tcp://*:2430",
            config_provider=self._config_provider,
            worker_map=self.detection_manager.worker_map,
            workers_by_name=self.detection_manager.workers_by_name,
            worker_lock=self.detection_manager.worker_lock,
            cloud_api=self._cloud_api,
        )
        self.command_controller.start()

        # 监控线程模块
        self.monitoring_thread = MonitoringThread(self.detection_manager)
        try:
            monitoring_started = self.monitoring_thread.start()
        except Exception:
            logger.exception("监控线程启动异常")
            self.stop()
            return False
        if not monitoring_started:
            logger.error("监控线程启动失败")
            self.stop()
            return False

        logger.info("patrol_robot_h1 启动完成, %d个线程运行中", len(self.detection_manager))
        return True

    def stop(self):
        logger.info("patrol_robot_h1 停止中...")

        # 先停止监控，避免检测 Worker 注册表清理时仍读取统计。
        if self.monitoring_thread:
            self.monitoring_thread.stop()

        # 再停止接收新命令，避免关闭 worker 时又收到动态控制请求。
        if self.command_controller:
            self.command_controller.stop()

        if self.detection_manager:
            self.detection_manager.stop_all()

        # 停止视频拉流
        if self.high_definition_hub:
            self.high_definition_hub.stop()
        if self.infrared_thermal_hub:
            self.infrared_thermal_hub.stop()

        # 关闭ZMQ
        if self.zmq_sender:
            self.zmq_sender.close()

        elapsed = (self.monitoring_thread.uptime_seconds if self.monitoring_thread else 0.0)
        logger.info(f"patrol_robot_h1 已停止, 运行时长: {elapsed / 3600:.2f} 小时")

    # 阻塞主线程，直到监控线程退出。
    def wait(self):
        if self.monitoring_thread:
            self.monitoring_thread.join()

def main():
    service = PatrolAIService()

    def signal_handler(sig, frame):
        logger.info(f"收到信号 {sig}, 停止服务...")
        service.stop()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    if service.start():
        service.wait()
        service.stop()

if __name__ == "__main__":
    main()
