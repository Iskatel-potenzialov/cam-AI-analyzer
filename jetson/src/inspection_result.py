#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
inspection_result.py

Builds and saves one normalized JSON result for a single domino inspection.

This module does NOT run YOLO or DINO itself.
It receives already calculated values from the inspection pipeline and writes
a transport-ready inspection_result.json that can later be sent to Ubuntu.

Compatible with Python 3.6+.
"""

import json
from datetime import datetime
from pathlib import Path


SCHEMA_VERSION = "1.0"


def _to_float(value):
    return None if value is None else float(value)


def _to_int(value):
    return None if value is None else int(value)


def _normalize_bbox(bbox):
    if bbox is None:
        return None
    return [float(v) for v in bbox]


def _normalize_scores(anomaly_map):
    """
    Accepts:
      - nested Python list [rows][cols]
      - numpy-like object with .tolist()

    Returns:
      nested list of plain float values suitable for JSON.
    """
    if hasattr(anomaly_map, "tolist"):
        anomaly_map = anomaly_map.tolist()

    return [
        [float(value) for value in row]
        for row in anomaly_map
    ]


def _normalize_regions(regions):
    """
    Expected region item example:
    {
        "row": 10,
        "col": 4,
        "score": 0.523421,
        "bbox_crop": [80, 434, 100, 478],
        "center_crop": [90, 456]
    }
    """
    normalized = []

    for index, region in enumerate(regions or [], start=1):
        bbox = region.get("bbox_crop", region.get("bbox"))
        center = region.get("center_crop", region.get("center"))

        item = {
            "rank": int(region.get("rank", index)),
            "row": int(region["row"]),
            "col": int(region["col"]),
            "score": float(region["score"]),
            "bbox_crop": [int(v) for v in bbox] if bbox is not None else None,
            "center_crop": [int(v) for v in center] if center is not None else None,
        }
        normalized.append(item)

    return normalized


def build_inspection_result(
    inspection_id,
    device_id,
    result,
    detection_class,
    detection_confidence,
    detection_bbox,
    anomaly_model,
    matching,
    anomaly_threshold,
    ignore_border_patches,
    anomaly_map,
    anomaly_regions,
    score_min,
    score_mean,
    score_p95,
    score_max,
    sharpness,
    brightness,
    crop_width,
    crop_height,
    artifacts,
    timestamp=None,
    embedding_dim=384,
):
    """
    Create one JSON-serializable inspection result dictionary.

    result:
        "GOOD" or "DEFECT"

    anomaly_map:
        complete 14x14 anomaly score map (or another grid size)

    artifacts:
        dict with file names / relative paths, e.g.
        {
            "full": "01_full.jpg",
            "full_bbox": "02_full_bbox.jpg",
            "crop": "03_crop.jpg",
            "heatmap": "07_anomaly_heatmap.jpg",
            "overlay": "08_anomaly_overlay.jpg",
            "anomalies": "09_top_anomalies_filtered.jpg"
        }
    """

    scores = _normalize_scores(anomaly_map)

    if not scores or not scores[0]:
        raise ValueError("anomaly_map must not be empty")

    rows = len(scores)
    cols = len(scores[0])

    for row in scores:
        if len(row) != cols:
            raise ValueError("anomaly_map rows have different lengths")

    result = str(result).upper()
    if result not in ("GOOD", "DEFECT"):
        raise ValueError("result must be GOOD or DEFECT")

    if timestamp is None:
        timestamp = datetime.now().astimezone().isoformat()

    payload = {
        "schema_version": SCHEMA_VERSION,
        "inspection_id": str(inspection_id),
        "device_id": str(device_id),
        "timestamp": str(timestamp),

        "result": result,

        "detection": {
            "class": str(detection_class),
            "confidence": _to_float(detection_confidence),
            "bbox": _normalize_bbox(detection_bbox),
        },

        "anomaly": {
            "model": str(anomaly_model),
            "embedding_dim": int(embedding_dim),
            "matching": str(matching),

            "threshold": float(anomaly_threshold),
            "ignore_border_patches": int(ignore_border_patches),

            "score_min": _to_float(score_min),
            "score_mean": _to_float(score_mean),
            "score_p95": _to_float(score_p95),
            "score_max": _to_float(score_max),

            "grid": {
                "rows": rows,
                "cols": cols,
                "patch_count": rows * cols,
            },

            "scores": scores,
            "regions": _normalize_regions(anomaly_regions),
        },

        "quality": {
            "sharpness": _to_float(sharpness),
            "brightness": _to_float(brightness),
            "crop_width": _to_int(crop_width),
            "crop_height": _to_int(crop_height),
        },

        "artifacts": dict(artifacts or {}),
    }

    return payload


def save_inspection_result(payload, output_path):
    """
    Save inspection JSON with stable UTF-8 formatting.
    """
    output_path = Path(output_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", encoding="utf-8") as f:
        json.dump(
            payload,
            f,
            ensure_ascii=False,
            indent=2,
            sort_keys=False,
        )

    return output_path


def build_and_save_inspection_result(output_path, **kwargs):
    """
    Convenience helper:
        payload = build_inspection_result(...)
        save_inspection_result(payload, ...)
    """
    payload = build_inspection_result(**kwargs)
    saved_path = save_inspection_result(payload, output_path)
    return payload, saved_path
