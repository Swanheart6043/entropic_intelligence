from fastapi import APIRouter, HTTPException
from services.config_center import get_config_manager
from api.schemas import ConfigUpdateRequest

router = APIRouter(prefix="/api/configs", tags=["configs"])

@router.get("")
async def get_config():
    cm = get_config_manager()
    config = cm.read_config()
    if "error" in config:
        raise HTTPException(status_code=404, detail=config["error"])
    return {"success": True, "data": config}

@router.put("")
async def update_config(request: ConfigUpdateRequest):
    cm = get_config_manager()
    return cm.update_config(request.config)