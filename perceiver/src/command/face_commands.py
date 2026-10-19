"""人脸库同步命令及后台任务管理。"""

import logging
import threading
import time
import uuid
from concurrent.futures import ThreadPoolExecutor

import cv2
import numpy as np
from common.cloud_api import CloudApi


logger = logging.getLogger("FaceCommands")


class FaceCommands:
    MAX_GALLERY_SIZE = 100
    DOWNLOAD_WORKERS = 4
    MAX_JOB_HISTORY = 20
    METADATA_FIELDS = (
        "id",
        "sn",
        "age",
        "gender",
        "certificateId",
        "certificateType",
        "groupName",
        "personTypeName",
    )

    def __init__(self, worker_map: dict, cloud_api: CloudApi):
        self._worker_map = worker_map
        self._cloud_api = cloud_api

        self._job_lock = threading.Lock()
        self._jobs: dict[str, dict] = {}
        self._active_job_id: str | None = None
        self._closed = False
        self._stop_event = threading.Event()
        self._executor = ThreadPoolExecutor(
            max_workers=1,
            thread_name_prefix="FaceSync",
        )

    def _download_image(self, url: str) -> np.ndarray | None:
        """从 URL 下载图片，直接返回 cv2 Mat，不落盘。"""
        try:
            img_bytes = self._cloud_api.download_image(url)
            img_array = np.frombuffer(img_bytes, dtype=np.uint8)
            return cv2.imdecode(img_array, cv2.IMREAD_COLOR)
        except Exception as exc:
            logger.warning("图片下载失败: %s -> %s", url, exc)
            return None

    def handle_sync(self, msg: dict) -> dict:
        """提交人脸库同步任务，立即返回 job_id。"""
        face_worker = self._worker_map.get("face")
        if not face_worker:
            return {
                "error": "worker_not_found",
                "message": "人脸线程没在运行",
            }

        faces = msg.get("faces")
        if not isinstance(faces, list) or not faces:
            return {
                "error": "invalid_params",
                "message": "人脸数组不能为空",
            }
        if len(faces) > self.MAX_GALLERY_SIZE:
            return {
                "error": "exceed_limit",
                "message": f"max {self.MAX_GALLERY_SIZE} faces, got {len(faces)}",
            }

        job_id = uuid.uuid4().hex
        job = {
            "job_id": job_id,
            "state": "pending",
            "total": len(faces),
            "processed": 0,
            "success": 0,
            "failed": [],
            "gallery_size": face_worker.engine.get_gallery_size(),
            "created_at": time.time(),
        }
        with self._job_lock:
            if self._closed:
                return {
                    "error": "service_stopping",
                    "message": "人脸同步服务正在停止",
                }
            if self._active_job_id is not None:
                return {
                    "error": "busy",
                    "message": "已有人脸同步任务在运行",
                    "job_id": self._active_job_id,
                }
            self._prune_jobs_locked()
            self._jobs[job_id] = job
            self._active_job_id = job_id

        # 拷贝请求数据，避免调用方在后台任务期间修改原始对象。
        faces_snapshot = [
            dict(entry) if isinstance(entry, dict) else entry
            for entry in faces
        ]
        sn = msg.get("sn", "")
        try:
            self._executor.submit(
                self._run_sync,
                job_id,
                face_worker,
                faces_snapshot,
                sn,
            )
        except Exception as exc:
            with self._job_lock:
                self._active_job_id = None
                self._jobs[job_id].update({
                    "state": "failed",
                    "error": str(exc),
                    "finished_at": time.time(),
                })
            return {
                "error": "submit_failed",
                "message": str(exc),
            }

        return {
            "status": "accepted",
            "job_id": job_id,
            "state": "pending",
            "total": len(faces),
        }

    def handle_sync_status(self, msg: dict) -> dict:
        """查询人脸库同步任务状态。"""
        job_id = msg.get("job_id")
        if not isinstance(job_id, str) or not job_id:
            return {
                "error": "invalid_params",
                "message": "job_id must be a non-empty string",
            }
        with self._job_lock:
            job = self._jobs.get(job_id)
            if job is None:
                return {
                    "error": "job_not_found",
                    "message": f"face sync job not found: {job_id}",
                }
            return self._job_snapshot(job)

    def close(self) -> None:
        """停止接收新任务，并等待已提交的同步任务安全结束。"""
        with self._job_lock:
            if self._closed:
                return
            self._closed = True
            self._stop_event.set()
        self._executor.shutdown(wait=True, cancel_futures=True)

    def _run_sync(
        self,
        job_id: str,
        face_worker,
        faces: list,
        sn,
    ) -> None:
        self._update_job(job_id, state="running", started_at=time.time())
        try:
            downloads = self._download_faces(faces)
            registrations = []
            failed = []

            for entry, img, failure in downloads:
                if failure is not None:
                    failed.append(failure)
                    self._update_progress(job_id, len(registrations), failed)
                    continue

                name = entry["name"]
                embedding = face_worker.engine.extract_embedding(img)
                if embedding is None:
                    failed.append({"name": name, "reason": "未检测到人脸"})
                else:
                    meta = {
                        key: entry[key]
                        for key in self.METADATA_FIELDS
                        if key in entry
                    }
                    meta["sn"] = sn
                    registrations.append((name, embedding, meta))
                self._update_progress(job_id, len(registrations), failed)

            if not registrations:
                message = "没有可注册的人脸，已保留旧图库"
                self._update_job(
                    job_id,
                    state="failed",
                    processed=len(faces),
                    success=0,
                    failed=failed,
                    error=message,
                    finished_at=time.time(),
                )
                logger.warning("人脸同步失败: %s", message)
                return
            if self._stop_event.is_set():
                message = "服务停止，已取消人脸图库替换"
                self._update_job(
                    job_id,
                    state="failed",
                    processed=len(faces),
                    success=len(registrations),
                    failed=failed,
                    error=message,
                    finished_at=time.time(),
                )
                logger.warning("人脸同步失败: %s", message)
                return

            gallery_path = face_worker.cfg.get("gallery_path")
            gallery_size = face_worker.engine.replace_gallery(
                registrations,
                gallery_path,
            )
            self._update_job(
                job_id,
                state="succeeded",
                processed=len(faces),
                success=len(registrations),
                failed=failed,
                gallery_size=gallery_size,
                finished_at=time.time(),
            )
            logger.info(
                "人脸同步完成: %d/%d 成功, 失败: %s",
                len(registrations),
                len(faces),
                [item["name"] for item in failed],
            )
        except Exception as exc:
            logger.error("人脸同步失败: %s", exc, exc_info=True)
            self._update_job(
                job_id,
                state="failed",
                error=str(exc),
                finished_at=time.time(),
            )
        finally:
            with self._job_lock:
                if self._active_job_id == job_id:
                    self._active_job_id = None

    def _download_faces(self, faces: list):
        def download(entry):
            if self._stop_event.is_set():
                return {}, None, {
                    "name": "unnamed",
                    "reason": "服务正在停止",
                }
            if not isinstance(entry, dict):
                return {}, None, {
                    "name": "unnamed",
                    "reason": "人脸数据必须是对象",
                }
            name = entry.get("name")
            pic_path = entry.get("picUrl")
            if (
                not isinstance(name, str)
                or not name
                or not isinstance(pic_path, str)
                or not pic_path
            ):
                return entry, None, {
                    "name": name or "unnamed",
                    "reason": "缺少name或picUrl",
                }
            img = self._download_image(pic_path)
            if img is None:
                return entry, None, {
                    "name": name,
                    "reason": "图片下载或解码失败",
                }
            return entry, img, None

        with ThreadPoolExecutor(
            max_workers=self.DOWNLOAD_WORKERS,
            thread_name_prefix="FaceDownload",
        ) as executor:
            # 分批并发下载，避免同时持有整个图库的解码图片。
            for start in range(0, len(faces), self.DOWNLOAD_WORKERS):
                batch = faces[start:start + self.DOWNLOAD_WORKERS]
                yield from executor.map(download, batch)

    def _update_progress(self, job_id: str, success: int, failed: list[dict]) -> None:
        self._update_job(
            job_id,
            processed=success + len(failed),
            success=success,
            failed=list(failed),
        )

    def _update_job(self, job_id: str, **changes) -> None:
        with self._job_lock:
            job = self._jobs.get(job_id)
            if job is not None:
                job.update(changes)

    @staticmethod
    def _job_snapshot(job: dict) -> dict:
        snapshot = dict(job)
        snapshot["failed"] = [dict(item) for item in job.get("failed", [])]
        return snapshot

    def _prune_jobs_locked(self) -> None:
        completed = [
            job_id
            for job_id, job in self._jobs.items()
            if job["state"] in {"succeeded", "failed"}
        ]
        excess = len(self._jobs) - self.MAX_JOB_HISTORY + 1
        for job_id in completed[:max(0, excess)]:
            self._jobs.pop(job_id, None)
