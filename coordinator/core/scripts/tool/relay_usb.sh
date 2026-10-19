#!/bin/bash

RULES_FILE="/etc/udev/rules.d/99-relay.rules"

echo "正在配置继电器 udev 规则..."

# 直接使用 tee 覆盖写入（不带 -a），一步完成创建/清空/写入
echo 'SUBSYSTEM=="tty", ATTRS{idVendor}=="1a86", ATTRS{idProduct}=="7523", MODE="0666", SYMLINK+="ttyUSBrelay"' | sudo tee "$RULES_FILE" > /dev/null

# 触发 udev 规则重新加载
sudo udevadm control --reload-rules
sudo udevadm trigger

echo "继电器 udev 规则加载完成！"