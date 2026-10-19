from types import MappingProxyType
from typing import Dict, Mapping, TypedDict

class ProcessSpec(TypedDict, total=False):
    description: str   # 前端展示用的中文名

_PROCESSES: Dict[str, ProcessSpec] = {
    "web_gateway": {"description": "Web 网关"},
    "task_center": {"description": "任务中心"},
    "monitor_center": {"description": "监控中心"},
    "edge_gateway": {"description": "边缘网关"},
    "audio_video_center": {"description": "音视频中心"},
}

# 运行时不可变
PROCESSES: Mapping[str, ProcessSpec] = MappingProxyType(_PROCESSES)