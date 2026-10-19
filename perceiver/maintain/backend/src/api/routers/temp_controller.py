# api/routers/temp_controller.py
"""温控仪路由. 注意用同步 def 而非 async def:
串口 I/O 是阻塞的(单次事务最长约 1s), 同步端点会被 FastAPI 丢进线程池执行,
不会卡住事件循环; 服务内部的 threading.Lock 保证串口事务互斥."""
from typing import Optional
from fastapi import APIRouter
from pydantic import BaseModel, Field
from services.temp_controller import get_temp_controller

router = APIRouter(prefix="/api/temp-controller", tags=["temp-controller"])

class TempParamsUpdateRequest(BaseModel):
    """全部可选, 只写提交的字段"""
    temp_unit: Optional[int] = Field(None, ge=0, le=1, description="0=摄氏 1=华氏")
    filter: Optional[int] = Field(None, ge=0, le=10)
    compensation: Optional[float] = Field(None, ge=-20, le=20)
    power_on_delay: Optional[int] = Field(None, ge=0, le=999)
    relay1_type: Optional[int] = Field(None, ge=0, le=4)
    relay1_setpoint: Optional[float] = Field(None, ge=-58, le=248)
    relay1_hysteresis: Optional[float] = Field(None, ge=0, le=248)
    relay1_on_delay: Optional[int] = Field(None, ge=0, le=999)
    relay1_off_delay: Optional[int] = Field(None, ge=0, le=999)
    relay2_type: Optional[int] = Field(None, ge=0, le=4)
    relay2_setpoint: Optional[float] = Field(None, ge=-58, le=248)
    relay2_hysteresis: Optional[float] = Field(None, ge=0, le=248)
    relay2_on_delay: Optional[int] = Field(None, ge=0, le=999)
    relay2_off_delay: Optional[int] = Field(None, ge=0, le=999)

class RelayForceRequest(BaseModel):
    relay: int = Field(..., ge=1, le=2, description="1 或 2")
    mode: int = Field(..., ge=0, le=2, description="0=正常 1=强制关闭 2=强制开启")

@router.get("/status")
def get_status():
    """当前温度 + 单位 + 继电器状态 (前端轮询用)"""
    return get_temp_controller().read_status()

@router.get("/params")
def get_params():
    """全部可编辑参数 (约2s, 前端加 loading)"""
    return get_temp_controller().read_params()

@router.put("/params")
def update_params(request: TempParamsUpdateRequest):
    """写入参数 (只写提交的字段, 逐项写入+读回校验)"""
    params = request.model_dump(exclude_none=True)
    if not params:
        return {"success": False, "message": "没有需要写入的参数"}
    return get_temp_controller().write_params(params)

@router.post("/relay-force")
def relay_force(request: RelayForceRequest):
    """继电器强制控制 (强制模式下面板显示'---', 测试完记得切回正常)"""
    return get_temp_controller().force_relay(request.relay, request.mode)