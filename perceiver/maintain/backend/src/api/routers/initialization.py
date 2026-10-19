from fastapi import APIRouter
from services.initialization import get_project_initializer
from api.schemas import InitializeRequest

router = APIRouter(prefix="/api", tags=["initialization"])

@router.post("/initialize")
async def initialize_project(req: InitializeRequest):
    initializer = get_project_initializer()
    return initializer.initialization(sn=req.sn, vkey=req.vkey, time_str=req.time)
    