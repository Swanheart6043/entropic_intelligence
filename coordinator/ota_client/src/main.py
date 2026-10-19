#!/usr/bin/env python3
import os
import shutil
import tarfile
import tempfile
import urllib.request
import subprocess
import time
import logging
import hashlib
import re

OTA_SERVER_URL = "http://10.168.1.100:8000"
TIMEOUT = 60 # 超时时长
RETRIES = 2 # 重试次数
MD5_SUFFIX = ".md5" # 后缀名
DOWNLOAD_DIR = tempfile.mkdtemp(prefix="ota_save_")
UPGRADE_DIR = "/home/heyn/patrol_robot_upgrade"
BACKUP_DIR = "/home/heyn/patrol_robot_backup"
VERSION_FILE_PATH = os.path.join(UPGRADE_DIR, "version.txt")

latest_version = ""

def run_cmd(cmd, capture_output=True, check=False):
    logging.info("Running: %s", " ".join(cmd))
    try:
        result = subprocess.run(cmd, capture_output=capture_output, text=True, check=check)
        if capture_output:
            logging.debug("Stdout: %s", result.stdout.strip())
            logging.debug("Stderr: %s", result.stderr.strip())
        return result
    except subprocess.CalledProcessError as e:
        logging.error("Command failed: %s", e)
        raise

def parse_md5_from_text(text):
    """从 md5 文件文本中解析第一个 32 字符 md5（hex），若找不到返回 None。"""
    m = re.search(r"([0-9a-fA-F]{32})", text)
    if m:
        return m.group(1).lower()
    return None

def write_version_file(path, version):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(str(version) + "\n")
    os.replace(tmp, path)

# 读取本地版本文件
def read_version_file(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read().strip()
    except FileNotFoundError:
        logging.warning("版本文件不存在: %s", path)
        return ""
    except Exception:
        logging.exception("读取版本文件失败")
        return ""

# 检查最新版本
def check_new_version(server_url, timeout):
    global latest_version
    with urllib.request.urlopen(server_url + "/api/latest_version", timeout=timeout) as resp:
        if resp.status != 200:
            logging.warning("无法获取最新版本")
            raise RuntimeError(f"无法获取最新版本: Http{resp.status}")    
        latest_version = resp.read().decode("utf-8").strip()
    logging.info("最新版本: %s", latest_version)

# 下载并校验完整性（内部包含重试, 整个流程放在一个带重试机制的循环里）
def download_verify_integrity():
    print()
    logging.info("准备开始下载...")
    saved_file = os.path.join(DOWNLOAD_DIR, f"update_{latest_version}.tar.gz")
    attempts = 0
    last_err = None

    for attempts in range(RETRIES):
        try:
            # 下更新包
            with urllib.request.urlopen(f"{OTA_SERVER_URL}/api/download/{latest_version}", timeout=TIMEOUT) as resp:
                if resp.status != 200:
                    raise RuntimeError(f"下载出错: HTTP{resp.status}")
                with open(saved_file, "wb") as f:
                    shutil.copyfileobj(resp, f)

            # 下md5, 如果远端 md5 文件不存在或解析失败，将抛出异常
            with urllib.request.urlopen(f"{OTA_SERVER_URL}/api/download-md5/{latest_version}", timeout=TIMEOUT) as resp:
                if resp.status != 200:
                    raise RuntimeError(f"下载MD5出错: {resp.status}")
                text = resp.read().decode("utf-8", errors="ignore")
                md5 = parse_md5_from_text(text)
                if not md5:
                    raise RuntimeError("无法从 md5 文件中解析到 32 位 md5")
                remote_md5 = md5
            
            # 算本地md5
            h = hashlib.md5()
            with open(saved_file, "rb") as f:
                while True:
                    chunk = f.read(8192)
                    if not chunk:
                        break
                    h.update(chunk)
            local_md5 = h.hexdigest()
            
            # 对比md5
            logging.info("远程md5=%s  本地md5=%s", remote_md5, local_md5)
            if remote_md5 != local_md5: 
                raise RuntimeError(f"MD5 校验失败")
            logging.info("MD5 校验通过")
        except Exception as e:
            last_err = e
            logging.warning("下载或MD5校验失败（尝试第%d次/%d次）：%s", attempts+1, RETRIES+1, e)
            # 删除损坏的文件，准备重试
            try:
                if os.path.exists(saved_file):
                    os.remove(saved_file)
            except Exception:
                logging.debug("删除损坏文件失败: %s", saved_file)
            if attempts > RETRIES:
                logging.error("重试用尽，仍然失败")
                raise last_err
            else:
                # small backoff
                time.sleep(1 + attempts)
    logging.info("下载完成! ")
    print()
    
def start_upgrade():
    logging.info("准备开始更新...")
    print("xxxxxxx %s", DOWNLOAD_DIR)
    downloaded_file = os.path.join(DOWNLOAD_DIR, f"update_{latest_version}.tar.gz")

    # 解包到临时目录
    extracted_dir = os.path.join(DOWNLOAD_DIR, "extracted")
    os.makedirs(extracted_dir, exist_ok=True)
    with tarfile.open(downloaded_file, "r:gz") as tar:
        tar.extractall(path=extracted_dir)

    # 备份当前版本
    if os.path.exists(BACKUP_DIR):
        logging.info("删除旧备份: %s", BACKUP_DIR)
        shutil.rmtree(BACKUP_DIR)
    logging.info("备份 %s -> %s", UPGRADE_DIR, BACKUP_DIR)
    shutil.copytree(UPGRADE_DIR, BACKUP_DIR)
    
    # 停止服务
    start_script = os.path.join(UPGRADE_DIR, "start_all.sh")
    if os.path.exists(start_script) and os.access(start_script, os.X_OK):
        run_cmd([start_script, "stop"])
    else:
        logging.warning("start_all.sh 不存在或不可执行: %s （跳过 stop）", start_script)

    # 替换目录
    if os.path.exists(UPGRADE_DIR):
        logging.info("删除旧安装目录: %s", UPGRADE_DIR)
        shutil.rmtree(UPGRADE_DIR)
    os.makedirs(UPGRADE_DIR, exist_ok=True)
    items = os.listdir(extracted_dir)
    if len(items) == 1 and os.path.isdir(os.path.join(extracted_dir, items[0])):
        top = os.path.join(extracted_dir, items[0])
        logging.info("检测到顶层目录 %s，移入 %s", top, UPGRADE_DIR)
        for name in os.listdir(top):
            shutil.move(os.path.join(top, name), UPGRADE_DIR)
    else:
        for name in items:
            shutil.move(os.path.join(extracted_dir, name), UPGRADE_DIR)

    # 启动服务
    # if os.path.exists(start_script) and os.access(start_script, os.X_OK):
    #     run_cmd([start_script, "start"])
    # else:
    #     logging.warning("start_all.sh 不存在或不可执行: %s （跳过 start）", start_script)
    logging.info("更新完成! ")

def verify_upgrade():
    logging.info("准备开始验证...")
    # if os.path.exists(start_script) and os.access(start_script, os.X_OK):
    #     status = run_cmd([start_script, "status"], capture_output=True)
    #     out = (status.stdout or "") + (status.stderr or "")
    #     if "STOPPED" in out.upper():
    #         logging.error("ERROR: Update failed, detected STOPPED in status output. 开始回滚")
    #         # 回滚
    #         if os.path.exists(INSTALL_DIR):
    #             shutil.rmtree(INSTALL_DIR)
    #         if os.path.exists(BACKUP_DIR):
    #             shutil.move(BACKUP_DIR, INSTALL_DIR)
    #         if os.path.exists(start_script) and os.access(start_script, os.X_OK):
    #             run_cmd([start_script, "start"])
    #         logging.error("回滚完成")
    # else:
    #     logging.warning("无法验证服务状态（start_all.sh 不存在或不可执行），请手动检查")
    logging.info("验证完成! ")

def main():
    try:
        logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s: %(message)s")
        
        check_new_version(OTA_SERVER_URL, TIMEOUT)

        # current_version = read_version_file(VERSION_FILE_PATH)
        current_version = "v1.2.0"
        if current_version == latest_version:
            logging.info("已经最新: %s", current_version)
            return
        logging.info("需升级: %s -> %s", current_version, latest_version)
        
        # 下载
        download_verify_integrity()
        
        # 更新
        start_upgrade()
        
        # 等待几秒后验证
        logging.info("等待 10 秒用于服务启动...")
        time.sleep(10)
        
        # 验证
        verify_upgrade()
        
        # 后处理
        # write_version_file()
        logging.info("成功更新到: %s", latest_version)
    except Exception:
        logging.exception("更新过程出现异常，尝试回滚")
        # 回滚
        # try:
        #     if os.path.exists(INSTALL_DIR):
        #         shutil.rmtree(INSTALL_DIR)
        #     if os.path.exists(BACKUP_DIR):
        #         shutil.move(BACKUP_DIR, INSTALL_DIR)
        #     # 尝试启动
        #     start_script = os.path.join(INSTALL_DIR, "start_all.sh")
        #     if os.path.exists(start_script) and os.access(start_script, os.X_OK):
        #         run_cmd([start_script, "start"])
        #     logging.info("回滚已完成（如果备份存在）")
        # except Exception:
        #     logging.exception("回滚失败")
    finally: 
        # 清理临时目录, 不管成功失败
        logging.info("开始清理临时目录...")
        # try:
        #     if os.path.exists(tmp_dir):
        #         shutil.rmtree(tmp_dir)
        # except Exception:
        #     logging.warning("删除临时文件失败: %s", tmp_dir)

if __name__ == "__main__":
    main()