# 主任务相关
低电量充电 充电完成 SOS按下 SOS挂断

### 低电量充电
```json
{
    "command": "low-power-charging",
    "content": ""
}
```

### 充电完成
```json
{
    "command": "charging-complete",
    "content": ""
}
```

### SOS按下
```json
{
    "command": "sos-pressed",
    "content": ""
}
```

### SOS挂断
```json
{
    "command": "sos-recovery",
    "content": ""
}
```

# 主控通信相关
控制灯 充电 解除急停

### 控制灯
参数
```json
{
  "type": "lighting_control",
  "params": { "type": "head_light", "is_on": true } // head_light 或 warning_light
}
```

### 充电
参数
```json
{
  "type": "charging",
}
```

### 取消充电
参数
```json
{
  "type": "cancel_charging",
}
```

### 解除急停
参数
```json
{
  "type": "release_emergency_stop",
}
```