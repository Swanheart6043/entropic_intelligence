from fastapi import APIRouter
from services.process_center import get_process_manager

router = APIRouter(prefix="/api/processes", tags=["processes"])

@router.get("")
async def get_all_processes():
    pm = get_process_manager()
    return {"success": True, "data": pm.get_all_status()}

@router.get("/{process_name}")
async def get_process_status(process_name: str):
    pm = get_process_manager()
    return {"success": True, "data": pm.get_status(process_name)}
    
@router.post("/restart-all") 
async def restart_all_processes():
    pm = get_process_manager()
    return pm.restart_all()

@router.post("/{process_name}/start")
async def start_process(process_name: str):
    pm = get_process_manager()
    return pm.start_process(process_name)
    
@router.post("/{process_name}/stop")
async def stop_process(process_name: str, force: bool = False):
    pm = get_process_manager()
    return pm.stop_process(process_name, force)
    
@router.post("/{process_name}/restart")
async def restart_process(process_name: str):
    pm = get_process_manager()
    return pm.restart_process(process_name)