# 家用巡检宠物机器人
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
	路由器 10.168.1.1
	账号: swanheart	
	密码: swanheart806178

	协调层 10.168.1.10 
	账号: heyn  
	密码: heyn_888

	感知层 10.168.1.10
	账号: root  
	密码: root

	控制层 10.168.1.20  
	账号: 无
	密码: 无

	认知层 10.168.1.30  
	账号: 无
	密码: 无