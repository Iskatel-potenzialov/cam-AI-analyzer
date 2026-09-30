#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
wait_remove_worker.py

YOLO-only process that waits until the current domino is removed.
The process exits afterwards, releasing TensorRT/PyCUDA memory.
"""

import sys

import cv2

sys.path.insert(0, "/home/nvideo/domino_inspection/src")

from detector_legacy import YoloTRT


PROJECT_ROOT = "/home/nvideo/domino_inspection"
EMPTY_FRAMES_REQUIRED = 10
YOLO_MIN_CONF = 0.50


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


def has_domino(model, frame):
    detections, _ = model.Inference(frame.copy())

    for detection in detections:
        if (
            detection["class"] == "dm"
            and float(detection["conf"]) >= YOLO_MIN_CONF
        ):
            return True

    return False


def main():
    print("Loading YOLO for WAIT_REMOVE only...")

    model = YoloTRT(
        library=PROJECT_ROOT + "/models/libmyplugins.so",
        engine=PROJECT_ROOT + "/models/best_domino_280424.engine",
        conf=0.1,
        yolo_ver="v5",
    )

    cap = cv2.VideoCapture(
        low_pipeline(),
        cv2.CAP_GSTREAMER,
    )

    if not cap.isOpened():
        raise RuntimeError("Could not open camera")

    empty_count = 0

    try:
        while True:
            ret, frame = cap.read()

            if not ret:
                raise RuntimeError("Camera read failed")

            if has_domino(model, frame):
                empty_count = 0
            else:
                empty_count += 1
                print(
                    "WAIT_REMOVE empty:",
                    "%d/%d"
                    % (
                        empty_count,
                        EMPTY_FRAMES_REQUIRED,
                    ),
                )

            if empty_count >= EMPTY_FRAMES_REQUIRED:
                print("Object removed.")
                return

    finally:
        cap.release()


if __name__ == "__main__":
    main()
