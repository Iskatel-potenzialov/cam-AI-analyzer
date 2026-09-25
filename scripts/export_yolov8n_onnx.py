#!/usr/bin/env python3
"""Export pretrained YOLOv8n COCO weights to a checked ONNX artifact."""

from __future__ import annotations

import os
import shutil
from pathlib import Path

import onnx
from ultralytics import YOLO


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WEIGHTS_DIR = PROJECT_ROOT / "models" / "yolov8n" / "weights"
ARTIFACTS_DIR = PROJECT_ROOT / "models" / "yolov8n" / "artifacts"
WEIGHTS_PATH = WEIGHTS_DIR / "yolov8n.pt"
ONNX_PATH = ARTIFACTS_DIR / "yolov8n.onnx"


def graph_shape(value: onnx.ValueInfoProto) -> list[int | str]:
    tensor_type = value.type.tensor_type
    return [
        dimension.dim_value
        if dimension.HasField("dim_value")
        else dimension.dim_param or "?"
        for dimension in tensor_type.shape.dim
    ]


def main() -> None:
    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)
    ARTIFACTS_DIR.mkdir(parents=True, exist_ok=True)

    # Ultralytics downloads a missing standard weight file into this explicit directory.
    os.chdir(WEIGHTS_DIR)
    model = YOLO(WEIGHTS_PATH.name)

    if not WEIGHTS_PATH.is_file():
        raise FileNotFoundError(f"YOLOv8n weights were not created at {WEIGHTS_PATH}")

    exported_path = Path(
        model.export(
            format="onnx",
            imgsz=640,
            batch=1,
            dynamic=False,
            simplify=False,
            opset=17,
            nms=False,
        )
    )
    if not exported_path.is_absolute():
        exported_path = (Path.cwd() / exported_path).resolve()
    if not exported_path.is_file():
        raise FileNotFoundError(f"Ultralytics did not create the expected ONNX file: {exported_path}")

    if exported_path != ONNX_PATH.resolve():
        shutil.move(str(exported_path), ONNX_PATH)

    onnx_model = onnx.load(ONNX_PATH)
    onnx.checker.check_model(onnx_model)

    print(f"Weights: {WEIGHTS_PATH}")
    print(f"ONNX: {ONNX_PATH}")
    print(f"ONNX size: {ONNX_PATH.stat().st_size} bytes")
    for value in onnx_model.graph.input:
        print(f"ONNX input: {value.name} {graph_shape(value)}")
    for value in onnx_model.graph.output:
        print(f"ONNX output: {value.name} {graph_shape(value)}")
    print("ONNX checker: PASS")


if __name__ == "__main__":
    main()