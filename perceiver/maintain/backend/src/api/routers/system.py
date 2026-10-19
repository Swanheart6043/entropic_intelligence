import psutil
from fastapi import APIRouter

router = APIRouter(prefix="/api/system", tags=["system"])

_VIRTUAL_FSTYPES = {"squashfs", "tmpfs", "devtmpfs", "overlay", "proc", "sysfs"}

_BOOT_MOUNTPOINTS = {"/boot", "/boot/efi", "/boot/firmware"}

def _is_boot_partition(mountpoint: str) -> bool:
    return mountpoint in _BOOT_MOUNTPOINTS or mountpoint.startswith("/boot/")

def _collect_disks():
    disks = []
    seen_devices = set()
    for part in psutil.disk_partitions(all=False):
        if part.fstype in _VIRTUAL_FSTYPES:
            continue
        if part.device.startswith("/dev/loop"):
            continue
        if _is_boot_partition(part.mountpoint):
            continue
        if part.device in seen_devices:
            continue
        seen_devices.add(part.device)
        try:
            usage = psutil.disk_usage(part.mountpoint)
        except (PermissionError, OSError):
            continue
        disks.append({
            "device": part.device,
            "mountpoint": part.mountpoint,
            "fstype": part.fstype,
            "total_gb": round(usage.total / 1024**3, 2),
            "used_gb": round(usage.used / 1024**3, 2),
            "percent": usage.percent,
        })
    return disks

@router.get("/stats")
async def get_system_stats():
    cpu_percent = psutil.cpu_percent(interval=1)
    memory = psutil.virtual_memory()

    return {
        "success": True,
        "data": {
            "cpu_percent": cpu_percent,
            "memory": {
                "total_gb": round(memory.total / 1024**3, 2),
                "used_gb": round(memory.used / 1024**3, 2),
                "percent": memory.percent,
            },
            "disks": _collect_disks(),
        },
    }