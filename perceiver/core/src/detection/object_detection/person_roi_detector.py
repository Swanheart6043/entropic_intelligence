"""基于共享人体框的通用 ROI 二阶段检测器。"""

import logging
from collections.abc import Callable, Iterable

import numpy as np

from .object_detector import ObjectDetector
from .object_snapshot import ObjectDetection


logger = logging.getLogger("PersonRoiDetector")


def _box_iou(box1, box2) -> float:
    x1 = max(box1[0], box2[0])
    y1 = max(box1[1], box2[1])
    x2 = min(box1[2], box2[2])
    y2 = min(box1[3], box2[3])
    intersection = max(0, x2 - x1) * max(0, y2 - y1)
    area1 = max(0, box1[2] - box1[0]) * max(0, box1[3] - box1[1])
    area2 = max(0, box2[2] - box2[0]) * max(0, box2[3] - box2[1])
    union = area1 + area2 - intersection
    return intersection / union if union > 0 else 0.0


class TemporalDetectionTracker:
    def __init__(
        self,
        confirm_frames=3,
        max_gap_frames=2,
        iou_threshold=0.20,
        max_lost=8,
    ):
        self.confirm_frames = confirm_frames
        self.max_gap_frames = max_gap_frames
        self.iou_threshold = iou_threshold
        self.max_lost = max_lost
        self.next_id = 0
        self.tracks = {}

    def clear(self) -> None:
        self.tracks.clear()

    def update(self, persons, detections_by_person):
        current_boxes = [person[:4] for person in persons]
        matched_tracks = {}
        matched_track_ids = set()

        pairs = []
        for person_index, current_box in enumerate(current_boxes):
            for track_id, track in self.tracks.items():
                iou = _box_iou(current_box, track["bbox"])
                if iou >= self.iou_threshold:
                    pairs.append((iou, person_index, track_id))
        pairs.sort(reverse=True, key=lambda item: item[0])

        used_persons = set()
        for _, person_index, track_id in pairs:
            if person_index in used_persons or track_id in matched_track_ids:
                continue
            matched_tracks[person_index] = track_id
            matched_track_ids.add(track_id)
            used_persons.add(person_index)

        for person_index, track_id in matched_tracks.items():
            track = self.tracks[track_id]
            track["bbox"] = current_boxes[person_index]
            track["lost_frames"] = 0
            if person_index in detections_by_person:
                track["hit_count"] += 1
                track["gap_count"] = 0
                track["detections"] = detections_by_person[person_index]
            else:
                track["gap_count"] += 1

            if track["gap_count"] > self.max_gap_frames:
                track["hit_count"] = 0
                track["confirmed"] = False
                track["detections"] = []
            else:
                track["confirmed"] = track["hit_count"] >= self.confirm_frames

        for person_index, current_box in enumerate(current_boxes):
            if person_index in matched_tracks:
                continue
            has_detection = person_index in detections_by_person
            track_id = self.next_id
            self.next_id += 1
            self.tracks[track_id] = {
                "bbox": current_box,
                "hit_count": 1 if has_detection else 0,
                "gap_count": 0 if has_detection else self.max_gap_frames + 1,
                "confirmed": has_detection and self.confirm_frames <= 1,
                "lost_frames": 0,
                "detections": detections_by_person.get(person_index, []),
            }
            matched_tracks[person_index] = track_id

        matched_ids = set(matched_tracks.values())
        for track_id in list(self.tracks):
            if track_id in matched_ids:
                continue
            self.tracks[track_id]["lost_frames"] += 1
            if self.tracks[track_id]["lost_frames"] > self.max_lost:
                del self.tracks[track_id]

        confirmed = {}
        for person_index, track_id in matched_tracks.items():
            track = self.tracks[track_id]
            if track["confirmed"] and track["detections"]:
                confirmed[person_index] = track["detections"]
        return confirmed


class PersonRoiDetector:
    def __init__(
        self,
        model_path,
        classes_num,
        score_thres,
        nms_thres,
        target_class_ids: set[int] | None = None,
        person_min_size=80,
        roi_height_ratio=0.82,
        crop_expand=0.20,
        near_distance_only=False,
        min_person_height_px_near=100,
        near_margin_ratio=0.15,
        candidate_filter: Callable | None = None,
        confirm_frames=3,
        max_gap_frames=2,
        iou_threshold=0.20,
        max_lost_frames=8,
        model_name="人体 ROI 目标检测",
        detector=None,
    ):
        self.person_min_size = person_min_size
        self.roi_height_ratio = min(1.0, max(0.1, roi_height_ratio))
        self.crop_expand = crop_expand
        self.near_distance_only = near_distance_only
        self.min_person_height_px_near = min_person_height_px_near
        self.near_margin_ratio = near_margin_ratio
        self.target_class_ids = target_class_ids
        self.candidate_filter = candidate_filter
        self.detector = detector or ObjectDetector(
            model_path=model_path,
            classes_num=classes_num,
            score_thres=score_thres,
            nms_thres=nms_thres,
            model_name=model_name,
        )
        self.tracker = TemporalDetectionTracker(
            confirm_frames=confirm_frames,
            max_gap_frames=max_gap_frames,
            iou_threshold=iou_threshold,
            max_lost=max_lost_frames,
        )
        self.last_person_detections = []
        self.last_crop_regions = []

    def _person_near_enough(self, bbox, image_width, image_height) -> bool:
        if not self.near_distance_only:
            return True
        x1, y1, x2, y2 = bbox
        height = max(1.0, float(y2 - y1))
        width = max(1.0, float(x2 - x1))
        aspect = width / height
        if not 0.2 <= aspect <= 5.0:
            return False
        if height >= image_height * 0.99 or width >= image_width * 0.99:
            return False
        minimum_height = self.min_person_height_px_near * (
            1.0 - self.near_margin_ratio
        )
        return height >= minimum_height

    def _select_persons(
        self,
        frame: np.ndarray,
        detections: Iterable[ObjectDetection],
    ):
        persons = []
        for detection in detections:
            if detection.class_name != "person":
                continue
            x1, y1, x2, y2 = detection.bbox
            if x2 - x1 < self.person_min_size or y2 - y1 < self.person_min_size:
                continue
            if not self._person_near_enough(
                detection.bbox,
                frame.shape[1],
                frame.shape[0],
            ):
                continue
            persons.append((x1, y1, x2, y2, detection.confidence))
        return persons

    def _crop_person_roi(self, frame: np.ndarray, bbox):
        image_height, image_width = frame.shape[:2]
        x1, y1, x2, y2 = bbox
        width = x2 - x1
        roi_height = int((y2 - y1) * self.roi_height_ratio)
        expand_x = int(width * self.crop_expand)
        expand_y = int(roi_height * self.crop_expand * 0.6)
        crop_box = (
            max(0, x1 - expand_x),
            max(0, y1 - expand_y),
            min(image_width, x2 + expand_x),
            min(image_height, y1 + roi_height + expand_y),
        )
        crop_x1, crop_y1, crop_x2, crop_y2 = crop_box
        return frame[crop_y1:crop_y2, crop_x1:crop_x2], crop_box

    def detect(
        self,
        frame: np.ndarray,
        object_detections: Iterable[ObjectDetection],
    ):
        persons = self._select_persons(frame, object_detections)
        self.last_person_detections = list(persons)
        self.last_crop_regions = []
        if not persons:
            self.tracker.clear()
            return [], []

        detections_by_person = {}
        for person_index, person in enumerate(persons):
            person_box = person[:4]
            crop, crop_box = self._crop_person_roi(frame, person_box)
            self.last_crop_regions.append(crop_box)
            if crop.size == 0 or crop.shape[0] < 20 or crop.shape[1] < 20:
                continue

            crop_x1, crop_y1, _, _ = crop_box
            mapped_detections = []
            for raw_detection in self.detector.detect(crop):
                class_id, score, x1, y1, x2, y2 = raw_detection
                if (
                    self.target_class_ids is not None
                    and class_id not in self.target_class_ids
                ):
                    continue
                if self.candidate_filter and not self.candidate_filter(
                    crop,
                    raw_detection,
                    person_box,
                ):
                    continue
                mapped_detections.append(
                    (
                        x1 + crop_x1,
                        y1 + crop_y1,
                        x2 + crop_x1,
                        y2 + crop_y1,
                        float(score),
                        int(class_id),
                    )
                )
            if mapped_detections:
                detections_by_person[person_index] = mapped_detections

        confirmed = self.tracker.update(persons, detections_by_person)
        detections = []
        events = []
        for person_index, target_detections in confirmed.items():
            person = persons[person_index]
            for x1, y1, x2, y2, score, class_id in target_detections:
                detections.append((class_id, score, x1, y1, x2, y2))
                events.append(
                    {
                        "person_bbox": list(person[:4]),
                        "target_bbox": [x1, y1, x2, y2],
                        "score": score,
                        "class_id": class_id,
                    }
                )
        return detections, events
