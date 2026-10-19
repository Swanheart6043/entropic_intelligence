# 网络间歇故障采集

适用于工控机上的 Ubuntu 20.04 / Python 3.8 及以上版本。仅使用 Python 标准库与系统已有的 `ip`、`ss`、`ps`、`systemctl`、`journalctl`。

本工具读取状态并写诊断日志，不修改 IP、路由、防火墙或业务服务，不向雷达、RDK、AGX 发送探测请求。应在工控机宿主系统运行，不要放进独立网络命名空间的容器。

## 采集内容

- `network-events.jsonl`：持续订阅网口链路、地址、路由和策略规则变化。短暂事件也会单独记录，不必等到定时快照。
- `system-events.jsonl`：本次启动的内核、systemd、NetworkManager、systemd-networkd 日志。可用于查链路、驱动、服务退出及关机流程；不会主动读取应用日志文件。
- `snapshots.jsonl`：默认每 30 秒记录地址、路由、接口收发统计、监听端口、待连接 TCP socket、关键进程与服务状态、内存和文件描述符数量。网络事件发生后补拍快照，频繁变化时快照至少间隔 5 秒。

每条记录包含带时区的时间、单调时钟和 Linux boot ID。校时会改变墙上时间，因此同一次启动内排序和计算间隔时可对照 `monotonic_s`。日志反映宿主系统网络状态，不能代替对业务请求或雷达数据的检查。

默认每类日志为 10 MiB 一卷、保留 5 卷历史，三类合计约 180 MiB，加上少量记录边界开销；新旧启动共用这套轮转额度。不是固定保留天数，频繁报错时旧记录会更快被覆盖。

## 先验证一次

将 `collect_network_evidence.py` 复制到工控机后，从其所在目录执行：

```bash
sudo python3 collect_network_evidence.py --once
sudo ls -lh /var/log/robot-network-evidence
```

前台持续运行：

```bash
sudo python3 collect_network_evidence.py
```

Ctrl+C 会停止采集。退出 SSH 会话可能结束前台进程，长期采集应使用下面的服务。同一输出目录一次只允许一个采集进程。

## 可选：安装为开机服务

以下步骤只安装诊断工具。先确认脚本及 `robot-network-evidence.service` 位于当前目录；若实际部署目录不同，先调整服务中的 `ExecStart`。

```bash
sudo install -d /home/heyn/patrol_robot_h1/tools
sudo install -m 0755 collect_network_evidence.py /home/heyn/patrol_robot_h1/tools/
sudo install -m 0644 robot-network-evidence.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now robot-network-evidence.service
sudo systemctl status robot-network-evidence.service --no-pager
```

停止并取消开机启动：

```bash
sudo systemctl disable --now robot-network-evidence.service
```

此服务只自动重启采集器，不会重启机器人、网络服务或业务进程。

## 故障发生后

在重启机器人或重新插线前，记下现场时间及设备指示灯状态。采集器已经持续写文件，无需为了抓日志反复重启业务。

停止采集后打包完整目录，保留轮转文件；确认文件已取走后再恢复采集：

```bash
sudo systemctl stop robot-network-evidence.service
archive="/tmp/robot-network-evidence-$(date +%Y%m%d-%H%M%S).tar.gz"
sudo tar -czf "$archive" -C /var/log robot-network-evidence
sudo systemctl start robot-network-evidence.service
```

若使用前台方式，先 Ctrl+C，打包后重新运行脚本即可。

重点对照：

| 证据 | 下一步判断 |
|---|---|
| `eno1` 丢失 carrier，随后地址/本地路由被删除 | 查直接连接的网线、对端设备及供电；本机通过 `10.10.10.10` 访问服务可能连带失败 |
| IP 和本地路由仍在，但业务端口不监听或进程退出 | 查对应业务服务日志与异常退出原因 |
| 内存、FD 或线程持续增长 | 查资源泄漏；单独的重试计数不能证明泄漏 |
| 新 boot ID 出现，旧日志没有关机序列 | 考虑供电、看门狗、强制复位或崩溃；仅凭日志中断不能确定掉电 |

本机快照里的 `10.10.10.10` 是此项目的工控机地址；如果现场改了地址，请同步调整脚本中的路由查询目标。

## 与电源监测配合

电源监测点应尽量靠近工控机实际输入端，记录电压、最低电压、欠压事件及持续时间，并与工控机对时。若监测设备与工控机共用故障电源，需要具备独立供电/保持能力或掉电可保留的事件记录。

本工具每次快照结束都会刷新日志到磁盘，但硬盘缓存、突然掉电或系统完全卡死仍可能丢失最后一段记录；它不能取代电压瞬态监测，也不能仅凭日志末尾判断供电不足。

协议参考：[ip monitor](https://man7.org/linux/man-pages/man8/ip-monitor.8.html)、[journalctl](https://www.freedesktop.org/software/systemd/man/255/journalctl.html)。
