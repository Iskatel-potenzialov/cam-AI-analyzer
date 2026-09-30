import os
import sys
import time
import json
from datetime import datetime

import cv2
import numpy as np

sys.path.insert(0, "/home/nvideo/domino_inspection/src")
from detector_legacy import YoloTRT

PROJECT_ROOT = "/home/nvideo/domino_inspection"
RESULT_ROOT = os.path.join(PROJECT_ROOT, "results")

LOW_W = 1280
LOW_H = 720
HIGH_W = 3264
HIGH_H = 2464

STABLE_FRAMES = 5
STABLE_CENTER_TOL = 0.03
STABLE_SIZE_TOL = 0.05

HIGH_CAPTURE_FRAMES = 5
MARGIN = 0.02


def usage():
    raise SystemExit(
        "Usage: python3 src/capture_experiment.py <experiment> <sample>\n"
        "Example: python3 src/capture_experiment.py experiment_01 good_reference"
    )


def safe_name(value):
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
    if not value or any(ch not in allowed for ch in value):
        raise ValueError(
            f"Invalid name '{value}'. Use only letters, digits, '_' and '-'."
        )
    return value


def low_pipeline():
    return (
        "nvarguscamerasrc ! "
        "video/x-raw(memory:NVMM), width=1280, height=720, framerate=30/1 ! "
        "nvvidconv ! "
        "video/x-raw, format=BGRx ! "
        "videoconvert ! "
        "video/x-raw, format=BGR ! "
        "appsink drop=true max-buffers=1 sync=false"
    )


def high_pipeline():
    return (
        "nvarguscamerasrc ! "
        "video/x-raw(memory:NVMM), width=3264, height=2464, framerate=21/1 ! "
        "nvvidconv ! "
        "video/x-raw, format=BGRx ! "
        "videoconvert ! "
        "video/x-raw, format=BGR ! "
        "appsink drop=true max-buffers=1 sync=false"
    )


def get_single_detection(model, frame):
    detections, inference_time = model.Inference(frame.copy())

    valid = [d for d in detections if d["class"] == "dm"]
    valid = [d for d in valid if float(d["conf"]) >= 0.5]

    if not valid:
        return None, inference_time

    best = max(valid, key=lambda d: float(d["conf"]))
    return best, inference_time


def box_geometry(box):
    x1, y1, x2, y2 = map(float, box)
    w = x2 - x1
    h = y2 - y1
    cx = (x1 + x2) / 2.0
    cy = (y1 + y2) / 2.0
    return cx, cy, w, h


def boxes_stable(previous, current, frame_w, frame_h):
    pcx, pcy, pw, ph = box_geometry(previous)
    ccx, ccy, cw, ch = box_geometry(current)

    center_delta = np.hypot(
        (ccx - pcx) / frame_w,
        (ccy - pcy) / frame_h,
    )
    width_delta = abs(cw - pw) / max(pw, 1.0)
    height_delta = abs(ch - ph) / max(ph, 1.0)

    return (
        center_delta <= STABLE_CENTER_TOL
        and width_delta <= STABLE_SIZE_TOL
        and height_delta <= STABLE_SIZE_TOL
    )


def crop_with_margin(frame, box, margin=MARGIN):
    h, w = frame.shape[:2]
    x1, y1, x2, y2 = map(float, box)

    bw = x2 - x1
    bh = y2 - y1
    mx = bw * margin
    my = bh * margin

    x1 = max(0, int(round(x1 - mx)))
    y1 = max(0, int(round(y1 - my)))
    x2 = min(w, int(round(x2 + mx)))
    y2 = min(h, int(round(y2 + my)))

    if x2 <= x1 or y2 <= y1:
        return None, None

    return frame[y1:y2, x1:x2].copy(), (x1, y1, x2, y2)


def quality_metrics(crop):
    gray = cv2.cvtColor(crop, cv2.COLOR_BGR2GRAY)
    sharpness = float(cv2.Laplacian(gray, cv2.CV_64F).var())
    brightness = float(gray.mean())
    return sharpness, brightness


def draw_detection(frame, detection, thickness):
    result = frame.copy()
    x1, y1, x2, y2 = map(int, map(round, detection["box"]))
    conf = float(detection["conf"])

    cv2.rectangle(result, (x1, y1), (x2, y2), (0, 255, 0), thickness)

    label = f"dm  conf={conf:.3f}"
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 1.4 if frame.shape[1] > 2000 else 0.8
    text_thickness = 3 if frame.shape[1] > 2000 else 2
    (tw, th), baseline = cv2.getTextSize(label, font, scale, text_thickness)

    tx = max(0, x1)
    ty = max(th + 12, y1 - 12)

    cv2.rectangle(
        result,
        (tx, ty - th - 12),
        (tx + tw + 16, ty + baseline + 6),
        (0, 0, 0),
        -1,
    )
    cv2.putText(
        result,
        label,
        (tx + 8, ty - 4),
        font,
        scale,
        (0, 255, 0),
        text_thickness,
        cv2.LINE_AA,
    )
    return result


def save_image(path, image):
    if not cv2.imwrite(path, image):
        raise RuntimeError(f"Failed to save: {path}")
    print("saved:", path)


def main():
    if len(sys.argv) != 3:
        usage()

    experiment = safe_name(sys.argv[1])
    sample = safe_name(sys.argv[2])

    output_dir = os.path.join(RESULT_ROOT, experiment, sample)
    os.makedirs(output_dir, exist_ok=True)

    model = YoloTRT(
        library=os.path.join(PROJECT_ROOT, "models/libmyplugins.so"),
        engine=os.path.join(PROJECT_ROOT, "models/best_domino_280424.engine"),
        conf=0.1,
        yolo_ver="v5",
    )

    print("OUTPUT:", output_dir)
    print("SEARCH: opening 1280x720 camera")

    cap = cv2.VideoCapture(low_pipeline(), cv2.CAP_GSTREAMER)
    if not cap.isOpened():
        raise RuntimeError("Could not open low-resolution camera")

    stable_count = 0
    previous_box = None
    stable_frame = None
    stable_detection = None

    while True:
        ret, frame = cap.read()
        if not ret:
            cap.release()
            raise RuntimeError("Low-resolution camera read failed")

        detection, inference_time = get_single_detection(model, frame)

        if detection is None:
            stable_count = 0
            previous_box = None
            print("SEARCH: waiting for one dm")
            continue

        box = detection["box"]

        if previous_box is not None and boxes_stable(
            previous_box,
            box,
            frame.shape[1],
            frame.shape[0],
        ):
            stable_count += 1
        else:
            stable_count = 1

        previous_box = np.array(box, dtype=np.float32).copy()

        print(
            "STABILIZE:",
            f"{stable_count}/{STABLE_FRAMES}",
            "conf=", round(float(detection["conf"]), 3),
            "box=", [round(float(v), 1) for v in box],
        )

        if stable_count >= STABLE_FRAMES:
            stable_frame = frame.copy()
            stable_detection = dict(detection)
            break

    cap.release()

    save_image(os.path.join(output_dir, "00_search_full.jpg"), stable_frame)
    save_image(
        os.path.join(output_dir, "00_search_bbox.jpg"),
        draw_detection(stable_frame, stable_detection, 3),
    )

    print("STABLE: object confirmed")
    print("CAPTURE: switching to 3264x2464")
    time.sleep(1.0)

    cap = cv2.VideoCapture(high_pipeline(), cv2.CAP_GSTREAMER)
    if not cap.isOpened():
        raise RuntimeError("Could not open high-resolution camera")

    print("CAPTURE: warming up high-resolution camera")

    for _ in range(20):
        ret, _ = cap.read()
        if not ret:
            cap.release()
            raise RuntimeError("High-resolution camera warm-up failed")

    print("CAPTURE: high-resolution camera ready")

    candidates = []

    for index in range(HIGH_CAPTURE_FRAMES):
        ret, frame = cap.read()
        if not ret:
            continue

        detection, inference_time = get_single_detection(model, frame)

        if detection is None:
            print(f"HIGH {index}: detection failed")
            continue

        crop, crop_box = crop_with_margin(frame, detection["box"])

        if crop is None or crop.size == 0:
            print(f"HIGH {index}: invalid crop")
            continue

        sharpness, brightness = quality_metrics(crop)

        print(
            f"HIGH {index}:",
            "conf=", round(float(detection["conf"]), 3),
            "sharpness=", round(sharpness, 1),
            "brightness=", round(brightness, 1),
            "box=", [round(float(v), 1) for v in detection["box"]],
        )

        candidates.append(
            {
                "frame": frame.copy(),
                "detection": dict(detection),
                "crop": crop,
                "crop_box": crop_box,
                "sharpness": sharpness,
                "brightness": brightness,
            }
        )

    cap.release()

    if not candidates:
        raise RuntimeError("No valid high-resolution candidates")

    best = max(candidates, key=lambda item: item["sharpness"])

    full = best["frame"]
    crop = best["crop"]
    high_detection = best["detection"]

    save_image(os.path.join(output_dir, "01_full.jpg"), full)
    save_image(
        os.path.join(output_dir, "02_full_bbox.jpg"),
        draw_detection(full, high_detection, 8),
    )
    save_image(os.path.join(output_dir, "03_crop.jpg"), crop)

    metadata = {
        "experiment": experiment,
        "sample": sample,
        "captured_at": datetime.now().astimezone().isoformat(),
        "camera": {
            "search_resolution": [LOW_W, LOW_H],
            "inspection_resolution": [HIGH_W, HIGH_H],
        },
        "search_detection": {
            "class": str(stable_detection["class"]),
            "confidence": float(stable_detection["conf"]),
            "bbox": [float(v) for v in stable_detection["box"]],
        },
        "highres_detection": {
            "class": str(high_detection["class"]),
            "confidence": float(high_detection["conf"]),
            "bbox": [float(v) for v in high_detection["box"]],
            "crop_bbox_with_margin": [int(v) for v in best["crop_box"]],
        },
        "quality": {
            "sharpness": float(best["sharpness"]),
            "brightness": float(best["brightness"]),
        },
        "crop": {
            "height": int(crop.shape[0]),
            "width": int(crop.shape[1]),
            "channels": int(crop.shape[2]),
            "margin_fraction": MARGIN,
        },
        "selection": {
            "highres_candidates": len(candidates),
            "criterion": "maximum_laplacian_variance",
        },
    }

    metadata_path = os.path.join(output_dir, "metadata.json")
    with open(metadata_path, "w", encoding="utf-8") as f:
        json.dump(metadata, f, ensure_ascii=False, indent=2)

    print("saved:", metadata_path)
    print()
    print("CAPTURE COMPLETE")
    print("sample:", sample)
    print("confidence:", round(float(high_detection["conf"]), 3))
    print("sharpness:", round(float(best["sharpness"]), 1))
    print("brightness:", round(float(best["brightness"]), 1))
    print("crop shape:", crop.shape)
    print("output:", output_dir)


if __name__ == "__main__":
    main()
