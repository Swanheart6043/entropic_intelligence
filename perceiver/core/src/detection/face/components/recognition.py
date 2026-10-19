import argparse
import time
import logging
import numpy as np
from .engine import FaceEngine
from rtsp_capture import RTSPCapture

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(name)s] %(levelname)s %(message)s")
logger = logging.getLogger("face_test")

def main():
    args = parse_args()

    logger.info("加载模型...")
    t0 = time.time()
    engine = FaceEngine(det_model=args.det_model, rec_model=args.rec_model, det_size=tuple(args.det_size), match_thresh=args.match_thresh)
    logger.info(f"模型加载完成, 耗时 {time.time() - t0:.1f}s")

    if args.gallery:
        engine.load_gallery(args.gallery)

    cap = RTSPCapture(args.rtsp_url)
    if not cap.cap or not cap.cap.isOpened():
        logger.error("RTSP 连接失败"); return

    frame_count = 0
    det_times, rec_times = [], []
    skip = max(1, int(cap.fps * args.interval))
    logger.info(f"开始处理, mode={args.mode}, FPS={cap.fps:.1f}, 每 {skip} 帧处理一次")

    try:
        while True:
            ret, frame = cap.read()
            if not ret: continue
            frame_count += 1
            if frame_count % skip != 0: continue

            if args.mode == "detect":
                t1 = time.time()
                faces_raw = engine.detector.detect(frame)
                t_det = time.time() - t1
                det_times.append(t_det)
                logger.info(f"{len(faces_raw)} 张脸 | 检测={t_det*1000:.0f}ms")
                for i, f in enumerate(faces_raw):
                    bbox = f["bbox"].astype(int)
                    logger.info(f"  脸#{i}: bbox={bbox.tolist()}, conf={f['score']:.2f}")

            else:
                # recognize 模式: 检测 + 特征 + 比对
                t1 = time.time()
                faces_raw = engine.detector.detect(frame)
                t_det = time.time() - t1
                det_times.append(t_det)

                t2 = time.time()
                results = []
                for f in faces_raw:
                    emb = engine.recognizer.get_embedding(frame, f["landmarks"])
                    identity, similarity = engine._match(emb)
                    results.append((f, identity, similarity))
                t_rec = time.time() - t2
                if faces_raw: rec_times.append(t_rec)

                logger.info(f"{len(results)} 张脸 | 检测={t_det*1000:.0f}ms, 识别={t_rec*1000:.0f}ms, 总={(t_det+t_rec)*1000:.0f}ms")
                for i, (f, ident, sim) in enumerate(results):
                    bbox = f["bbox"].astype(int)
                    name = ident or "unknown"
                    logger.info(f"  脸#{i}: {name} (sim={sim:.2f}), bbox={bbox.tolist()}, conf={f['score']:.2f}")

            if len(det_times) % 5 == 0 and det_times:
                avg_d = np.mean(det_times)*1000
                avg_r = np.mean(rec_times)*1000 if rec_times else 0
                logger.info(f"=== avg: det={avg_d:.0f}ms, rec={avg_r:.0f}ms ===")

    except KeyboardInterrupt:
        logger.info("停止")
    finally:
        cap.release()
        if det_times:
            logger.info(f"\n===== 最终统计 =====")
            logger.info(f"  帧数: {len(det_times)}")
            d = np.array(det_times)*1000
            logger.info(f"  检测: avg={d.mean():.0f}ms, min={d.min():.0f}ms, max={d.max():.0f}ms")
            if rec_times:
                r = np.array(rec_times)*1000
                logger.info(f"  识别: avg={r.mean():.0f}ms, min={r.min():.0f}ms, max={r.max():.0f}ms")

def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--rtsp-url", default="rtsp://admin:heyn_888@10.10.10.40:554/id=1&type=0")
    p.add_argument("--det-model", default="/home/root/models/buffalo_s/det_500m.onnx")
    p.add_argument("--rec-model", default="/home/root/models/buffalo_s/w600k_mbf.onnx")
    p.add_argument("--mode", choices=["detect", "recognize"], default="detect")
    p.add_argument("--det-size", type=int, nargs=2, default=[320, 320])
    p.add_argument("--match-thresh", type=float, default=0.4)
    p.add_argument("--interval", type=float, default=2.0)
    p.add_argument("--gallery", default=None, help="人脸库 JSON 路径")
    return p.parse_args()

if __name__ == "__main__":
    main()
