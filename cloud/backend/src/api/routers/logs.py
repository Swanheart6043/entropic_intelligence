from typing import Optional
from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from loguru import logger
from services.log_center import get_log_collector

router = APIRouter(tags=["logs"])

@router.get("/api/logs")
async def get_logs(process: Optional[str] = None, level: Optional[str] = None, limit: int = 100):
    lc = get_log_collector()
    logs = lc.get_recent_logs(process, level, limit)
    return {"success": True, "count": len(logs), "data": logs}

@router.websocket("/ws/logs")
async def websocket_logs(websocket: WebSocket):
    """WebSocket 实时推送日志"""
    await websocket.accept()
    lc = get_log_collector()
    lc.add_ws_client(websocket)

    logger.info(f"WebSocket client connected from {websocket.client}")
    try:
        while True:
            data = await websocket.receive_text()
            if data == "ping":
                await websocket.send_text("pong")
    except WebSocketDisconnect:
        logger.info(f"WebSocket client disconnected from {websocket.client}")
        lc.remove_ws_client(websocket)
    except Exception as e:
        logger.exception(f"WebSocket error: {e}")
        lc.remove_ws_client(websocket)