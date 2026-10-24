from typing import Optional, Dict, Any
from pydantic import BaseModel, Field

class InitializeRequest(BaseModel):
    sn: str = Field(..., description="设备序列号", example="H1202501001")
    vkey: str = Field(..., description="NPC密钥", example="heyn000002@2025")
    time: Optional[str] = Field(None, description="系统时间", example="2025-01-15 10:30:00")
    auto_reboot: bool = Field(False, description="是否自动重启")

class ConfigUpdateRequest(BaseModel):
    config: Dict[str, Dict[str, Any]] = Field(
        ...,
        description="嵌套配置 {section: {key: value}}",
        example={
            "device": {"sn": "H1202603022"},
            "platform": {
                "websocket_url": "wss://cloud.example.com/websocket/robot/{sn}",
                "http_base_url": "https://cloud.example.com/robotboot",
                "rtsp_base_url": "rtsp://media.example.com:2554",
                "tls_verify": True,
            },
        },
    )
