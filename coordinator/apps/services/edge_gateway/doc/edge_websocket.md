# 边缘websocket接口
地址: ws://10.10.10.10:9002

### 获取设备信息
```json
{
    "command": "device-info",
    "content": {
        "humidity": 0,
        "humidityText": "0%",
        "isCharging": false,
        "isHeadLightOpen": false,
        "isStop": false,
        "isWarningLightOpen": false,
        "isWipersOpen": false,
        "mode": "auto",
        "power": 0,
        "powerText": "0%",
        "sn": "H1202501001",
        "temperature": 0,
        "temperatureText": "0°C",
        "totalMileage": 0,
        "totalMileageText": "0km",
        "wipersMode": "single",
        "volume": "5"
    }
}
```

### 显示标语
```json
{
    "command": "display-slogan",
    "content": [] // 来自平台
}
```

### 播文本
```json
{
    "command": "broadcast-text",
    "content": {
		"Soundtext": "", //播报内容
		"Priority": "", //播报类型 7：人脸识别 告警播报，10：实时下发音播报
		"BroadcastCnt": "", //
		"BroadcastInv": "", //时间间隔
		"Duration": "" //持续时间（条件添加）
    }
}
```

### 播音频
```json
{
    "command": "broadcast-wav",
    "content": {
        "wav": "",
        "volume": 10, // 0-10
    }
}
```

### 控制音量
```json
{
    "command": "volume",
    "content": "6" // 1-10
}
```

### 监听和对讲
```json
{
    "command": "audio",
    "content": {
        "voiceType": "listening", // listening, talk
        "status": 1 // 0关闭 1开启
    }
}
```

### SOS
```json
{
    "command": "sos",
    "content": ""
}
```

### SOS结束
```json
{
    "command": "sos-end",
    "content": ""
}
```

### 到达目标点
```json
{
    "command": "arrival-point",
    "content": "point_name"
}
```