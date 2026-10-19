### 检测到吸烟
```json
{
  "command": "alarm",
  "type": "smoking",
  "content": "吸烟有害健康"
}
```

### 检测到垃圾
```json
{
  "command": "alarm",
  "type": "rubbish",
  "content": "发现地面垃圾",
  "gps": {
    "asl": 0,
    "direction": 0,
    "ew": 0,
    "latitude": 0,
    "longitude": 0,
    "sn": 0,
    "speed": 0,
    "valid": 0
  }
}
```

# 进程间接口 - 感知中心

### 校时
```json
{
  "type": "synchronize_time",
  "date": 1
}
```