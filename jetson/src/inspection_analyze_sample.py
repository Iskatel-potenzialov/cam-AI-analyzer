#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
inspection_analyze_sample.py

DINO-only worker.

Usage:
    python3 src/inspection_analyze_sample.py production inspection_... --send

This worker intentionally does NOT import detector_legacy / TensorRT / PyCUDA.
"""

import argparse
import json
import re
import sys
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F

sys.path.insert(0, "/home/nvideo/domino_inspection/src")

from inspection_result import build_and_save_inspection_result
from inspection_sender import send_inspection


PROJECT_ROOT = Path("/home/nvideo/domino_inspection")
RESULT_ROOT = PROJECT_ROOT / "results"

GOOD_REFERENCE_PATH = (
    PROJECT_ROOT
    / "results"
    / "experiment_01"
    / "good_reference"
    / "03_crop.jpg"
)

DEVICE_ID = "jetson_nano_01"
MODEL_NAME = "dino_vits16"
MATCHING = "all_to_all_good_bank"

IMAGE_SIZE = 224
GRID_SIZE = 14
ANOMALY_THRESHOLD = 0.40
VISUAL_MAX = 0.55
IGNORE_BORDER_PATCHES = 1


def safe_name(value):
    allowed = set(
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789_-"
    )
    if not value or any(ch not in allowed for ch in value):
        raise ValueError("Unsafe experiment/sample name: %s" % value)
    return value


def make_safe_inspection_id(metadata):
    raw = (
        metadata.get("experiment", "inspection")
        + "_"
        + metadata.get("sample", "sample")
        + "_"
        + metadata["captured_at"]
    )
    safe = re.sub(r"[^A-Za-z0-9_-]+", "_", raw)
    safe = re.sub(r"_+", "_", safe).strip("_")
    if not safe:
        raise RuntimeError("Cannot build safe inspection_id")
    return safe


def prepare_image(path, device):
    bgr = cv2.imread(str(path))
    if bgr is None:
        raise RuntimeError("Cannot read image: %s" % path)

    rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    rgb = cv2.resize(
        rgb,
        (IMAGE_SIZE, IMAGE_SIZE),
        interpolation=cv2.INTER_AREA,
    )

    tensor = torch.from_numpy(rgb).float() / 255.0
    tensor = tensor.permute(2, 0, 1)

    mean = torch.tensor(
        [0.485, 0.456, 0.406]
    ).view(3, 1, 1)
    std = torch.tensor(
        [0.229, 0.224, 0.225]
    ).view(3, 1, 1)

    tensor = (tensor - mean) / std

    return tensor.unsqueeze(0).to(device), bgr


def get_patch_embeddings(model, tensor):
    with torch.no_grad():
        tokens = model.get_intermediate_layers(tensor, n=1)[0]

    patches = tokens[:, 1:, :]
    expected = GRID_SIZE * GRID_SIZE

    if patches.shape[1] != expected:
        raise RuntimeError(
            "Unexpected patch count: %s; expected %d"
            % (tuple(patches.shape), expected)
        )

    return F.normalize(patches, dim=-1)


def calculate_distance_map(reference_patches, test_patches):
    good_bank = reference_patches[0]
    test_bank = test_patches[0]

    similarity_matrix = torch.matmul(
        test_bank,
        good_bank.transpose(0, 1),
    )

    best_similarity, _ = similarity_matrix.max(dim=1)
    distance = 1.0 - best_similarity

    return (
        distance.reshape(GRID_SIZE, GRID_SIZE)
        .detach()
        .cpu()
        .numpy()
    )


def patch_bbox(row, col, image_shape):
    height, width = image_shape[:2]

    x1 = int(round(col * width / float(GRID_SIZE)))
    y1 = int(round(row * height / float(GRID_SIZE)))
    x2 = int(round((col + 1) * width / float(GRID_SIZE)))
    y2 = int(round((row + 1) * height / float(GRID_SIZE)))

    return [x1, y1, x2, y2]


def find_regions(distance_map, image_shape):
    regions = []

    for row in range(GRID_SIZE):
        for col in range(GRID_SIZE):
            if (
                row < IGNORE_BORDER_PATCHES
                or row >= GRID_SIZE - IGNORE_BORDER_PATCHES
                or col < IGNORE_BORDER_PATCHES
                or col >= GRID_SIZE - IGNORE_BORDER_PATCHES
            ):
                continue

            score = float(distance_map[row, col])

            if score < ANOMALY_THRESHOLD:
                continue

            bbox = patch_bbox(row, col, image_shape)
            center = [
                int(round((bbox[0] + bbox[2]) / 2.0)),
                int(round((bbox[1] + bbox[3]) / 2.0)),
            ]

            regions.append(
                {
                    "row": row,
                    "col": col,
                    "score": score,
                    "bbox_crop": bbox,
                    "center_crop": center,
                }
            )

    regions.sort(
        key=lambda item: item["score"],
        reverse=True,
    )

    for rank, region in enumerate(regions, start=1):
        region["rank"] = rank

    return regions


def make_visuals(distance_map, crop_bgr):
    height, width = crop_bgr.shape[:2]
    work = distance_map.copy()

    border = IGNORE_BORDER_PATCHES
    if border > 0:
        work[:border, :] = ANOMALY_THRESHOLD
        work[-border:, :] = ANOMALY_THRESHOLD
        work[:, :border] = ANOMALY_THRESHOLD
        work[:, -border:] = ANOMALY_THRESHOLD

    strength = np.clip(
        (work - ANOMALY_THRESHOLD)
        / max(VISUAL_MAX - ANOMALY_THRESHOLD, 1e-8),
        0.0,
        1.0,
    )

    mask_small = (strength > 0).astype(np.uint8) * 255

    heat_small = cv2.applyColorMap(
        (strength * 255).astype(np.uint8),
        cv2.COLORMAP_JET,
    )

    heat_big = cv2.resize(
        heat_small,
        (width, height),
        interpolation=cv2.INTER_NEAREST,
    )
    mask_big = cv2.resize(
        mask_small,
        (width, height),
        interpolation=cv2.INTER_NEAREST,
    )

    anomaly_only = np.zeros(
        (height, width, 3),
        dtype=np.uint8,
    )
    anomaly_only[mask_big > 0] = heat_big[mask_big > 0]

    alpha = cv2.resize(
        strength,
        (width, height),
        interpolation=cv2.INTER_NEAREST,
    )
    alpha = np.clip(alpha * 0.90, 0.0, 0.90)[..., None]

    overlay = (
        crop_bgr.astype(np.float32) * (1.0 - alpha)
        + anomaly_only.astype(np.float32) * alpha
    )
    overlay = np.clip(
        overlay,
        0,
        255,
    ).astype(np.uint8)

    return anomaly_only, overlay


def draw_regions(crop_bgr, regions):
    result = crop_bgr.copy()

    for region in regions:
        x1, y1, x2, y2 = region["bbox_crop"]

        cv2.rectangle(
            result,
            (x1, y1),
            (x2, y2),
            (0, 0, 255),
            2,
        )

        cv2.putText(
            result,
            "#%d %.3f" % (
                region["rank"],
                region["score"],
            ),
            (x1, max(24, y1 - 8)),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.65,
            (0, 0, 255),
            2,
            cv2.LINE_AA,
        )

    return result


def save_image(path, image):
    if not cv2.imwrite(str(path), image):
        raise RuntimeError("Failed to save: %s" % path)
    print("saved:", path)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("experiment")
    parser.add_argument("sample")
    parser.add_argument("--send", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()

    experiment = safe_name(args.experiment)
    sample = safe_name(args.sample)

    sample_dir = RESULT_ROOT / experiment / sample
    metadata_path = sample_dir / "metadata.json"
    crop_path = sample_dir / "03_crop.jpg"

    if not metadata_path.exists():
        raise RuntimeError("Missing metadata.json: %s" % metadata_path)

    if not crop_path.exists():
        raise RuntimeError("Missing crop: %s" % crop_path)

    with metadata_path.open("r", encoding="utf-8") as handle:
        metadata = json.load(handle)

    device = torch.device(
        "cuda" if torch.cuda.is_available() else "cpu"
    )

    print("DINO worker PID:", __import__("os").getpid())
    print("Loading DINO only...")
    model = torch.hub.load(
        "facebookresearch/dino:main",
        MODEL_NAME,
        pretrained=True,
    ).to(device).eval()

    print("Loading GOOD reference...")
    reference_tensor, _ = prepare_image(
        GOOD_REFERENCE_PATH,
        device,
    )
    reference_patches = get_patch_embeddings(
        model,
        reference_tensor,
    )

    print("Analyzing test crop...")
    test_tensor, test_bgr = prepare_image(
        crop_path,
        device,
    )
    test_patches = get_patch_embeddings(
        model,
        test_tensor,
    )

    distance_map = calculate_distance_map(
        reference_patches,
        test_patches,
    )

    flat = distance_map.reshape(-1)

    score_min = float(np.min(flat))
    score_mean = float(np.mean(flat))
    score_p95 = float(np.percentile(flat, 95))
    score_max = float(np.max(flat))

    regions = find_regions(
        distance_map,
        test_bgr.shape,
    )

    result = "DEFECT" if regions else "GOOD"

    heatmap, overlay = make_visuals(
        distance_map,
        test_bgr,
    )
    boxes = draw_regions(
        test_bgr,
        regions,
    )

    save_image(
        sample_dir / "07_anomaly_heatmap.jpg",
        heatmap,
    )
    save_image(
        sample_dir / "08_anomaly_overlay.jpg",
        overlay,
    )
    save_image(
        sample_dir / "09_top_anomalies_filtered.jpg",
        boxes,
    )

    highres = metadata["highres_detection"]
    quality = metadata["quality"]
    crop = metadata["crop"]

    inspection_id = make_safe_inspection_id(metadata)

    artifacts = {
        "search_full": "00_search_full.jpg",
        "search_bbox": "00_search_bbox.jpg",
        "full": "01_full.jpg",
        "full_bbox": "02_full_bbox.jpg",
        "crop": "03_crop.jpg",
        "heatmap": "07_anomaly_heatmap.jpg",
        "overlay": "08_anomaly_overlay.jpg",
        "anomalies": "09_top_anomalies_filtered.jpg",
    }

    _, saved_path = build_and_save_inspection_result(
        output_path=sample_dir / "inspection_result.json",
        inspection_id=inspection_id,
        device_id=DEVICE_ID,
        timestamp=metadata["captured_at"],
        result=result,
        detection_class=highres["class"],
        detection_confidence=highres["confidence"],
        detection_bbox=highres["bbox"],
        anomaly_model=MODEL_NAME,
        matching=MATCHING,
        anomaly_threshold=ANOMALY_THRESHOLD,
        ignore_border_patches=IGNORE_BORDER_PATCHES,
        anomaly_map=distance_map,
        anomaly_regions=regions,
        score_min=score_min,
        score_mean=score_mean,
        score_p95=score_p95,
        score_max=score_max,
        sharpness=quality["sharpness"],
        brightness=quality["brightness"],
        crop_width=crop["width"],
        crop_height=crop["height"],
        artifacts=artifacts,
        embedding_dim=384,
    )

    print()
    print("INSPECTION RESULT")
    print("result       :", result)
    print("inspection_id:", inspection_id)
    print("threshold    :", ANOMALY_THRESHOLD)
    print("score mean   :", round(score_mean, 6))
    print("score p95    :", round(score_p95, 6))
    print("score max    :", round(score_max, 6))
    print("regions      :", len(regions))
    print("saved        :", saved_path)

    if args.send:
        print()
        print("Uploading to Ubuntu...")
        send_inspection(sample_dir)


if __name__ == "__main__":
    main()
