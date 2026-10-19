"""日志收集服务 - 通过ZMQ接收各C++进程的日志"""
import zmq
import json
import asyncio
from datetime import datetime
from loguru import logger
from typing import Optional, Dict, List
from collections import deque

class LogCollector:
    """
    初始化日志收集器
    Args:
        zmq_port: ZMQ监听端口
        max_buffer_size: 内存中保留的最大日志条数
    """
    def __init__(self, zmq_port: int = 5556, max_buffer_size: int = 1000):
        self.context = zmq.Context()
        self.subscriber = self.context.socket(zmq.SUB)
        self.subscriber.bind(f"tcp://*:{zmq_port}")
        self.subscriber.subscribe("")
        
        # 设置为非阻塞
        self.subscriber.setsockopt(zmq.RCVTIMEO, 100)
        
        # 内存缓冲区（最近的日志）
        self.log_buffer = deque(maxlen=max_buffer_size)
        
        # WebSocket客户端列表
        self.ws_clients: List = []
        
        self.running = False
        
        logger.info(f"日志中心已初始化tcp://*:{zmq_port}")
    
    async def start(self):
        self.running = True
        logger.info("启动日志收集...")
        while self.running:
            try:
                # 非阻塞接收
                msg = self.subscriber.recv_string(zmq.NOBLOCK)
                await self.process_log(msg)
            except zmq.Again:
                # 没有消息，继续
                await asyncio.sleep(0.01)
            except Exception as e:
                logger.error(f"Error receiving log: {e}")
                await asyncio.sleep(0.1)
    
    """处理接收到的日志"""
    async def process_log(self, msg: str):
        try:
            log_entry = json.loads(msg)
            
            # 添加接收时间
            log_entry['received_at'] = datetime.now().isoformat()
            
            # 存入内存缓冲区
            self.log_buffer.append(log_entry)
            
            # 广播给所有WebSocket客户端
            await self.broadcast_to_clients(log_entry)
            
        except json.JSONDecodeError as e:
            logger.error(f"Invalid JSON log: {msg[:100]}... Error: {e}")
        except Exception as e:
            logger.error(f"Error processing log: {e}")
    
    async def broadcast_to_clients(self, log_entry: Dict):
        """广播日志到所有WebSocket客户端"""
        if not self.ws_clients:
            return
        
        # 移除已断开的客户端
        disconnected = []
        for client in self.ws_clients:
            try:
                await client.send_json(log_entry)
            except Exception:
                disconnected.append(client)
        
        for client in disconnected:
            self.ws_clients.remove(client)
    
    def add_ws_client(self, client):
        """添加WebSocket客户端"""
        self.ws_clients.append(client)
        logger.info(f"WebSocket client added. Total: {len(self.ws_clients)}")
    
    def remove_ws_client(self, client):
        """移除WebSocket客户端"""
        if client in self.ws_clients:
            self.ws_clients.remove(client)
            logger.info(f"WebSocket client removed. Total: {len(self.ws_clients)}")
    
    def get_recent_logs(self, process: Optional[str] = None, level: Optional[str] = None, limit: int = 100) -> List[Dict]:
        """
        获取最近的日志 Args:
            process: 过滤进程名
            level: 过滤日志级别
            limit: 返回条数限制
        """
        logs = list(self.log_buffer)
        
        # 过滤
        if process:
            logs = [log for log in logs if log.get('process') == process]
        if level:
            logs = [log for log in logs if log.get('level') == level]
        
        # 返回最新的N条
        return logs[-limit:]
    
    def stop(self):
        """停止日志收集"""
        self.running = False
        self.subscriber.close()
        self.context.term()
        logger.info("Log collector stopped")

# 全局实例
log_collector = None

def get_log_collector() -> LogCollector:
    """获取日志收集器单例"""
    global log_collector
    if log_collector is None:
        log_collector = LogCollector()
    return log_collector
