#!/usr/bin/env python3
"""SSH/SCP客户端封装"""
import os
import sys
from pathlib import Path
from .logger import logger
try:
    import paramiko
    from scp import SCPClient
except ImportError:
    print("缺少依赖，正在安装...")
    os.system(f"{sys.executable} -m pip install paramiko scp")
    import paramiko
    from scp import SCPClient

class SSHClient:
    def __init__(self, host, port=22, username="root", password=None, key_path=None):
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.key_path = key_path
        self.client = None
        self.scp = None
    
    def connect(self):
        """建立SSH连接"""
        try:
            self.client = paramiko.SSHClient()
            self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            
            connect_kwargs = {
                "hostname": self.host,
                "port": self.port,
                "username": self.username,
            }
            
            if self.password:
                connect_kwargs["password"] = self.password
            elif self.key_path:
                connect_kwargs["key_filename"] = self.key_path
            
            self.client.connect(**connect_kwargs, timeout=10)
            self.scp = SCPClient(self.client.get_transport())
            logger.success(f"已连接到 {self.username}@{self.host}:{self.port}")
            return True
            
        except Exception as e:
            logger.error(f"SSH连接失败: {e}")
            return False
    
    def execute(self, command, show_output=True):
        """执行远程命令"""
        try:
            stdin, stdout, stderr = self.client.exec_command(command)
            exit_code = stdout.channel.recv_exit_status()
            output = stdout.read().decode('utf-8')
            error = stderr.read().decode('utf-8')
            
            if show_output and output:
                print(output, end='')
            if exit_code != 0:
                logger.warn(f"命令执行失败 (exit={exit_code}): {command}")
                if error:
                    print(error, end='', file=sys.stderr)
                return False, error
            return True, output
        except Exception as e:
            logger.error(f"执行命令异常: {e}")
            return False, str(e)
    
    def close(self):
        """关闭连接"""
        if self.scp:
            self.scp.close()
        if self.client:
            self.client.close()
        logger.info("SSH连接已关闭")

    def __enter__(self):
        if not self.connect():
            raise ConnectionError(f"无法连接到 {self.username}@{self.host}:{self.port}")
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()