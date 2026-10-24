# 家用监控宠物机器人
硬件架构，软件架构，组网，通信协议

# 硬件架构
RK3568板子，STM32板子，英伟达板子，云台，摄像头，路由器

# 软件架构
名称: cloud (云层)  
功能: 机群管理，流媒体服务，大模型推理、大模型训练  

	frontend
	backend

名称: coordinator (协调层RK3568)   
功能: 云端通信，任务调度，监控资源，边缘通信，音视频  

	core
	maintain
	ota_client

名称: control (控制层STM32)   
功能: 上传数据，执行控制，电池管理，充电对接

	core
	maintain
	ota_client

名称: perceiver (感知层RK3568)  
功能: 实时推理

	core
	maintain

cognition（认知层nvidia jetson nano）

	语义理解（VLM）
	core
	maintain
	ota_client

# 组网
	路由器 10.168.10.1
	账号: heyn@888wifi
	密码: heyn@888robot

	协调层 10.168.10.10 
	账号: heyn  
	密码: heyn_888

	感知层 10.168.10.10
	账号: root  
	密码: root

	控制层 10.168.10.20  
	账号: 无
	密码: 无

	认知层 10.168.10.30  
	账号: 无
	密码: 无

	高清摄像头 10.10.10.40  
	url: rtsp://admin:heyn_888@10.10.10.40:554/id=1&type=0
	账号: admin  密码: heyn_888  
	
	红外摄像头 10.10.10.41  
	url: rtsp://admin:heyn_888@10.10.10.41:554/cam/realmonitor?channel=1&subtype=0
	账号: admin  密码: heyn_888  