# 巡逻机器人H1
软硬件架构 组网  

coordinator (协调层) 工控机
- core
- maintain
- ota_client

perceiver (感知层) Ai板子
- core
- maintain

controller (控制层) 电源板
- core
- maintain
- ota_client

cognition (认知层)
任务规划、语义理解（LLM/VLM）
- core
- maintain
- ota_client

cloud (云层)
机群管理、大模型、训练

# 硬件架构
云台(摄像头) 

linux板:
	CPU: 英特尔
ai板子:
	SOC: Arm
	BPU: RDK
单片机板:
	MCU: STM32F407VGT6
云台:
	摄像头

# 组网
	路由器 10.10.10.1
	heyn@888wifi
	heyn@888robot

### 工控机
	账号: heyn  密码: heyn_888
	工控机: 10.10.10.10 

### 感知板
	账号: root  密码: root
	Ai板子1: 10.10.10.30
	Ai板子2: 10.10.10.31

### 电源板
	电源板: 10.10.10.20  

### 云台
	账号: admin  密码: heyn_888  
	id=1 第一通道  type=0 主码流  type=1 辅码流  
	摄像头高清:
		ip: 10.10.10.40  
		url: rtsp://admin:heyn_888@10.10.10.40:554/id=1&type=0
	摄像头红外:
		ip: 10.10.10.41  
		url: rtsp://admin:heyn_888@10.10.10.41:554/cam/realmonitor?channel=1&subtype=0
	摄像头前:
		ip: 10.10.10.42  
		url: rtsp://admin:heyn_888@10.10.10.42:554/id=1&type=0
	摄像头后: 
		ip: 10.10.10.43  
		url: rtsp://admin:heyn_888@10.10.10.43:554/id=1&type=0
	摄像头左: 
		ip: 10.10.10.44  
		url: rtsp://admin:heyn_888@10.10.10.44:554/id=1&type=0
	摄像头右: 
		ip: 10.10.10.45
		url: rtsp://admin:heyn_888@10.10.10.45:554/id=1&type=0

### 屏幕
	前屏幕: 10.10.10.6
	后屏幕: 10.10.10.7