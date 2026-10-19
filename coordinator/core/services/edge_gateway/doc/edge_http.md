# 边缘http接口
地址: 10.10.10.10:8088

### 登录
方式：post

url：/login

参数
```json
{
    "username": "heyn",
    "password": "heyn1688"
}
```

响应
```json
{
    "success": true,
    "message": "登录成功",
    "code": 200,
    "result": {
        "username": "heyn"
    },
    "timestamp": "时间戳"
}
```

### 获取设备信息
方式：get

url：/query-device-info

参数：无

响应
```json
{
    "success": true,
    "message": "查询成功",
    "code": 200,
    "result": { 
        "sn": "F2250206004",
        "mode": "auto", // auto 或 manual
        "power": 29.1129,
        "powerText": "29%",
        "temperature": 29.1129, // 温度
        "temperatureText": "29°C",
        "humidity": 29.1129, // 湿度
        "humidityText": "29%",
        "totalMileage": 333.33,
        "totalMileageText": "333.33km",
        "isCharging": false,
        "isHeadLightOpen": false,
        "isWarningLightOpen": false,
        "isWipersOpen": false,
        "wipersMode": "single", // single 或 continuous
        "isStop": false,
        "volume": "5"
    },
}
```

### 切换自动/手动
方式：post

url：/change-mode

参数: 
```json
{
    "mode": "auto", // auto 或 manual
}
```

响应
```json
{
    "success": true,
    "message": "切换成功",
    "code": 200,
    "result": null,
}
```

### 云台控制
方式：post

url：/control-ptz

参数
```json
{
    "direction": "up", // up, down, left, right, stop, reset, zoomOut(缩小), zoomIn(放大)
}
```

响应
```json
{
    "success": true,
    "message": "操作成功",
    "code": 200,
    "result": null,
}
```

### 一键充电
方式：post

url：/charging

参数：
```json
{
    "type": "charging" // charging 或 cancel
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 控制灯
方式：post

url：/control-light

参数
```json
{
    "type": "headLight", // headLight 或 warningLight
    "switch": "on" // on 或 off
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 控制雨刮器
方式：post

url：/control-wiper

参数
```json
{
    "type": "single", // single 或 continuous
    "switch": "on" // on 或 off，单刷时可以不传
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 控制音量
方式：post

url：/control-volume

参数
```json
{
    "volume": "5", // 1-10
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 播语音
方式：post

url：/broadcast-text

参数
```json
{
    "content": "我正在巡逻",
    "playMode": "single", // single 或 loop
    "duration": "1" // 1 分钟
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 查询版本
方式：get

url：/edge-hardware-version

参数：无

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": {
        "cognitionVersion": "0.0.0",
        "controlVersion": "1.5.0",
        "coordinatorVersion": "CH266.27.C-H1-v1.9.0",
        "navigationVersion": "V3.0.0.260509_nav3d_lidar32",
        "perceiveVersion": "CH266.27.A-H1-v1.9.0",
        "ptzVersion": "CH266.27.P-H1-v1.0.0",
        "screenVersion": "1.0.0"
    },
}
```

### 发送版本
方式：post

url：/edge-hardware-version

参数
```json
{
    "hardwareType": "screen", // screen屏幕 coordinator工控机 control主控板 perceiver算法板子 cognition大模型程序
    "version": "v1.0.0",
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": null,
}
```

### 获取算法类型
方式：get

url：/algorithm-type

参数：无

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": [
        {
            "name": "smoking",
            "remark": "吸烟检测"
        },
        {
            "name": "face",
            "remark": "人脸检测"
        },
        {
            "name": "licensePlate",
            "remark": "车牌检测"
        },
        {
            "name": "helmet",
            "remark": "安全帽检测"
        },
        {
            "name": "reflectiveVest",
            "remark": "反光衣检测"
        },
        {
            "name": "infraredHeat",
            "remark": "红外高温检测"
        },
        {
            "name": "fireSmoke",
            "remark": "火灾浓烟检测"
        }
    ],
}
```

### 获取算法区域
方式：get

url：/algorithm-area

参数：
```json
{
    "mapName": "gs1225",
}
```

响应
```json
{
    "success": true,
    "message": "请求成功",
    "code": 200,
    "result": [
        {
            "name": "smoking",
            "remark": "吸烟检测"
        },
        {
            "name": "face",
            "remark": "人脸检测"
        },
        {
            "name": "licensePlate",
            "remark": "车牌检测"
        },
        {
            "name": "helmet",
            "remark": "安全帽检测"
        },
        {
            "name": "reflectiveVest",
            "remark": "反光衣检测"
        },
        {
            "name": "infraredHeat",
            "remark": "红外高温检测"
        },
        {
            "name": "fireSmoke",
            "remark": "火灾浓烟检测"
        }
    ],
}
```
