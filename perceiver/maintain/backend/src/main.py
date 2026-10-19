import sys
import signal
import asyncio
import uvicorn
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from loguru import logger
from api.main import app
from services.log_center import get_log_collector
from common.paths import LOG_DIR

LOG_FORMAT = (
    "<green>{time:YYYY-MM-DD HH:mm:ss}</green> | "
    "<level>{level: <5}</level> | "
    "<cyan>{name}</cyan>:<cyan>{function}</cyan> - "
    "<level>{message}</level>"
)

logger.remove()
logger.add(sys.stderr, format=LOG_FORMAT)
logger.add(
    LOG_DIR / "main.log",
    format=LOG_FORMAT,
    rotation="100 MB",
    retention="7 days",
    level="DEBUG",
    enqueue=True, # 多进程/多线程写同一文件时强烈建议加上
    encoding="utf-8",
)

class MaintainSystem:
    def __init__(self):
        self.log_collector = get_log_collector()
        self._collector_task: asyncio.Task | None = None
    
    async def start(self):
        self._collector_task = asyncio.create_task(self.log_collector.start())
        # 启动Web服务
        config = uvicorn.Config(app, host="0.0.0.0", port=8000, log_level="info", access_log=True)
        server = uvicorn.Server(config)
        logger.info("=" * 60)
        logger.info("系统启动完成！")
        logger.info("http://10.10.10.10:8000 查看运维界面")
        logger.info("=" * 60)
        await server.serve()
    
    def stop(self):
        if self._collector_task and not self._collector_task.done():
            self._collector_task.cancel()
        self.log_collector.stop()
        logger.info("运维系统已停止")

async def main():
    system = MaintainSystem()
    try:
        await system.start()
    except Exception as e:
        logger.error(f"系统错误: {e}")
    finally:
        system.stop()

if __name__ == "__main__":
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    asyncio.run(main())