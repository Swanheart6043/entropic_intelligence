from loguru import logger
from pathlib import Path
from fastapi import FastAPI
from fastapi.staticfiles import StaticFiles
from fastapi.responses import RedirectResponse, JSONResponse
from common.paths import BUNDLED_WEB
from api.routers import initialization, system, processes, logs, configs, temp_controller

web_dir = Path(BUNDLED_WEB)

app = FastAPI(
    title="Patrol Robot Maintenance API",
    description="工业巡检机器人运维系统API",
    version="1.0.0",
)

@app.exception_handler(Exception)
async def unhandled_exception_handler(request, exc):
    logger.exception(f"{request.url.path} 异常: {exc}")
    return JSONResponse(status_code=500, content={"detail": str(exc)})

@app.get("/")
async def root():
    if web_dir.is_dir():
        return RedirectResponse(url="/index.html")
    else:
        return JSONResponse({"message": "API is running. Frontend should be served by its dev server."})

# 注册路由
app.include_router(initialization.router)
app.include_router(system.router)
app.include_router(processes.router)
app.include_router(logs.router)
app.include_router(configs.router)
app.include_router(temp_controller.router)

# 仅生产构建产物存在时挂载
if web_dir.is_dir():
    app.mount("/", StaticFiles(directory=str(web_dir), html=True), name="web")
else:
    logger.info("未发现前端构建目录，跳过静态资源挂载。")