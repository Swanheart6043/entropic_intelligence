import argparse
import cv2
import glob
import logging
import os
import sys
import time
from .engine import FaceEngine

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s %(message)s",
)
logger = logging.getLogger("face_reg")

def register_from_dir(engine: FaceEngine, face_dir: str):
    """从目录批量注册"""
    patterns = ["*.jpg", "*.jpeg", "*.png", "*.bmp"]
    files = []
    for p in patterns:
        files.extend(glob.glob(os.path.join(face_dir, p)))

    if not files:
        logger.error(f"目录为空或不存在: {face_dir}")
        return 0

    success = 0
    for f in sorted(files):
        name = os.path.splitext(os.path.basename(f))[0]
        img = cv2.imread(f)
        if img is None:
            logger.warning(f"✗ 无法读取: {f}")
            continue

        t0 = time.time()
        ok = engine.register(name, img)
        elapsed = (time.time() - t0) * 1000

        if ok:
            logger.info(f"✓ {name} ({elapsed:.0f}ms)")
            success += 1
        else:
            logger.warning(f"✗ {name} (未检测到人脸)")
    return success

def list_gallery(engine: FaceEngine):
    """列出人脸库中所有人"""
    names = engine.get_gallery_names()
    if not names:
        logger.info("人脸库为空")
        return
    logger.info(f"人脸库共 {len(names)} 人:")
    for i, name in enumerate(names):
        logger.info(f"  [{i + 1}] {name}")

def main():
    args = parse_args()

    # 初始化引擎
    engine = FaceEngine(
        det_model=args.det_model,
        rec_model=args.rec_model,
        det_size=tuple(args.det_size),
    )

    # 加载已有人脸库 (追加模式)
    if args.gallery and os.path.exists(args.gallery):
        engine.load_gallery(args.gallery)

    # --- 查看模式 ---
    if args.list:
        list_gallery(engine)
        return

    # --- 删除模式 ---
    if args.remove:
        if engine.remove(args.remove):
            logger.info(f"已删除: {args.remove}")
        else:
            logger.warning(f"未找到: {args.remove}")
        output = args.gallery or args.output
        engine.save_gallery(output)
        logger.info(f"人脸库已更新: {output}")
        return

    # --- 注册模式 ---
    if not args.face_dir:
        logger.error("请指定 --face-dir 注册目录")
        sys.exit(1)

    logger.info(f"开始注册, 目录: {args.face_dir}")
    count = register_from_dir(engine, args.face_dir)

    output = args.output
    engine.save_gallery(output)
    logger.info(f"注册完成: 成功 {count} 人, 人脸库共 {engine.get_gallery_size()} 人")
    logger.info(f"人脸库已保存: {output}")

def parse_args():
    p = argparse.ArgumentParser(description="人脸注册工具")
    p.add_argument("--face-dir", default=None, help="注册照片目录, 文件名为身份名")
    p.add_argument("--det-model", default="/home/root/models/buffalo_s/det_500m.onnx")
    p.add_argument("--rec-model", default="/home/root/models/buffalo_s/w600k_mbf.onnx")
    p.add_argument("--det-size", type=int, nargs=2, default=[320, 320])
    p.add_argument("--output", default="/home/root/face_gallery.json", help="人脸库输出路径")
    p.add_argument("--gallery", default=None, help="已有人脸库路径 (追加注册/查看/删除)")
    p.add_argument("--list", action="store_true", help="查看人脸库")
    p.add_argument("--remove", default=None, help="从人脸库删除指定人")
    return p.parse_args()

if __name__ == "__main__":
    main()
