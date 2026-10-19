#!/usr/bin/env python3
import cv2
import json
import base64
import logging
from time import time, sleep

from .result_image_store import ResultImageStore

try:
    import zmq
except:
    import os
    os.system("pip install pyzmq")
    import zmq

logger = logging.getLogger("RDK_YOLO")

# 颜色表
RDK_COLORS = [
    (56, 56, 255), (151, 157, 255), (31, 112, 255), (29, 178, 255),
    (49, 210, 207), (10, 249, 72), (23, 204, 146), (134, 219, 61),
    (52, 147, 26), (187, 212, 0), (168, 153, 44), (255, 194, 0),
    (147, 69, 52), (255, 115, 100), (236, 24, 0), (255, 56, 132),
]

class ZMQSender:
    """ZMQ 发送器 (PUSH 模式) - 带重连机制"""
    def __init__(self, endpoint, result_store=None):
        self.endpoint = endpoint
        self.context = None
        self.socket = None
        self.result_store = (
            result_store if result_store is not None else ResultImageStore()
        )
        self._connect()
    
    def _connect(self):
        """建立连接"""
        try:
            if self.socket:
                self.socket.close()
            if self.context:
                self.context.term()
        except:
            pass
        
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.PUSH)
        self.socket.setsockopt(zmq.SNDHWM, 10)  # 发送队列最大 10 条，防止积压
        self.socket.setsockopt(zmq.LINGER, 0)   # 关闭时不等待
        self.socket.connect(self.endpoint)
        logger.info(f"ZMQ PUSH 已连接到: {self.endpoint}")
        sleep(0.3)
    
    def send_detection(self, frame, results, class_names, frame_id=0, detection_type=""):
        """发送检测结果"""
        try:
            # 构建检测信息
            detections = []
            for class_id, score, x1, y1, x2, y2 in results:
                det = {
                    "class_id": int(class_id),
                    "class_name": class_names[class_id] if class_id < len(class_names) else f"class_{class_id}",
                    "confidence": round(float(score), 2),
                    "bbox": {
                        "x1": int(x1), "y1": int(y1),
                        "x2": int(x2), "y2": int(y2),
                        "width": int(x2 - x1), "height": int(y2 - y1)
                    }
                }
                detections.append(det)
            
            # 在图片上绘制检测框
            frame_to_send = frame.copy()
            for class_id, score, x1, y1, x2, y2 in results:
                color = RDK_COLORS[class_id % len(RDK_COLORS)]
                cv2.rectangle(frame_to_send, (int(x1), int(y1)), (int(x2), int(y2)), color, 2)
                class_name = class_names[class_id] if class_id < len(class_names) else f"class_{class_id}"
                label = f"{class_name}: {score:.2f}"
                (lw, lh), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
                cv2.rectangle(frame_to_send, (int(x1), int(y1) - lh - 5), (int(x1) + lw, int(y1)), color, -1)
                cv2.putText(frame_to_send, label, (int(x1), int(y1) - 3), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1)
            
            # 编码图像为 JPEG
            encode_ok, img_encoded = cv2.imencode('.jpg', frame_to_send, [cv2.IMWRITE_JPEG_QUALITY, 80])
            if not encode_ok:
                logger.error("JPEG 编码失败: frame_id=%s", frame_id)
                return False
            jpeg_bytes = img_encoded.tobytes()
            img_base64 = base64.b64encode(jpeg_bytes).decode('utf-8')
            
            # 构建消息
            confidences = [d["confidence"] for d in detections]
            message = {
                "type": "detection_result",
                "detection_type": detection_type,
                "frame_id": frame_id,
                "timestamp": time(),
                "image_width": frame.shape[1],
                "image_height": frame.shape[0],
                "detections": detections,
                "detection_count": len(detections),
                "avg_confidence": round(sum(confidences) / len(confidences), 2) if confidences else 0.0,
                "max_confidence": max(confidences) if confidences else 0.0,
                "all_confidences": ",".join(f"{c:.2f}" for c in confidences) if confidences else "",
                "image_base64": img_base64
            }
            
            sent = self.send_detection_message(message, jpeg_bytes)
            if sent:
                logger.info(f"已发送: frame_id={frame_id}, 检测数={len(detections)}")
            return sent
        except Exception as e:
            logger.error(f"ZMQ 发送异常: {e}")
            return False

    def send_detection_message(self, message, jpeg_bytes):
        """保存并发送已经构造好的 ZMQ 检测告警。"""
        detection_type = str(message.get("detection_type", ""))
        frame_id = message.get("frame_id", 0)
        try:
            saved_path = self.result_store.save_jpeg(
                jpeg_bytes,
                detection_type=detection_type,
                frame_id=frame_id,
            )
            logger.info("告警图片已保存: %s", saved_path)
        except Exception as exc:
            # 本地兜底失败不能阻断原有的 ZMQ 上报链路。
            logger.error("保存告警图片失败: %s", exc, exc_info=True)

        try:
            self.socket.send_string(
                json.dumps(message, ensure_ascii=False),
                zmq.NOBLOCK,
            )
            return True
        except zmq.Again:
            logger.warning("ZMQ 发送队列满，跳过此帧")
            return False
        except Exception as exc:
            logger.error("ZMQ 发送异常: %s", exc, exc_info=True)
            return False

    def send_door_alert(self, alarm_msg: dict):
        """发送门状态告警消息 (纯 JSON，不含图片)"""
        try:
            self.socket.send_string(json.dumps(alarm_msg), zmq.NOBLOCK)
            logger.info(f"已发送门告警: {alarm_msg.get('scene', '')} {alarm_msg.get('state', '')}")
            return True
        except zmq.Again:
            logger.warning("ZMQ 发送队列满，跳过门告警")
            return False
        except Exception as e:
            logger.error(f"ZMQ 门告警发送异常: {e}")
            return False

    def close(self):
        """关闭连接"""
        try:
            if self.socket:
                self.socket.close()
            if self.context:
                self.context.term()
        except:
            pass
