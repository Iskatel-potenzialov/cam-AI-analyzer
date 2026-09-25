#!/usr/bin/env python3
"""Export pretrained YOLOv8n COCO weights to a checked static ONNX artifact."""

from __future__ import annotations

import argparse
import os
import shutil
from pathlib import Path

import onnx
from ultralytics import YOLO


PROJECT_ROOT = Path(__file__).resolve().parents[1]
WEIGHTS_DIR = PROJECT_ROOT / "models" / "yolov8n" / "weights"
ARTIFACTS_DIR = PROJECT_ROOT / "models" / "yolov8n" / "artifacts"
WEIGHTS_PATH = WEIGHTS_DIR / "yolov8n.pt"


def graph_shape(value: onnx.ValueInfoProto) -> list[int | str]:
    tensor_type = value.type.tensor_type
    return [
        dimension.dim_value
        if dimension.HasField("dim_value")
        else dimension.dim_param or "?"
        for dimension in tensor_type.shape.dim
    ]


def artifact_path_for_batch(batch: int) -> Path:
    name = "yolov8n.onnx" if batch == 1 else f"yolov8n_b{batch}.onnx"
    return ARTIFACTS_DIR / name


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--batch",
        type=int,
        choices=(1, 2, 5),
        default=1,
        help="Static ONNX batch size; default preserves the existing batch-1 workflow.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    batch = args.batch
    onnx_path = artifact_path_for_batch(batch)
    expected_input_shape = [batch, 3, 640, 640]
    expected_output_shape = [batch, 84, 8400]

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
            batch=batch,
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

    if exported_path != onnx_path.resolve():
        shutil.move(str(exported_path), onnx_path)

    onnx_model = onnx.load(onnx_path)
    onnx.checker.check_model(onnx_model)
    input_shapes = [graph_shape(value) for value in onnx_model.graph.input]
    output_shapes = [graph_shape(value) for value in onnx_model.graph.output]
    if input_shapes != [expected_input_shape]:
        raise ValueError(f"Expected ONNX input shape {expected_input_shape}, got {input_shapes}")
    if output_shapes != [expected_output_shape]:
        raise ValueError(f"Expected ONNX output shape {expected_output_shape}, got {output_shapes}")

    print(f"Weights: {WEIGHTS_PATH}")
    print(f"Static batch: {batch}")
    print(f"ONNX: {onnx_path}")
    print(f"ONNX size: {onnx_path.stat().st_size} bytes")
    for value in onnx_model.graph.input:
        print(f"ONNX input: {value.name} {graph_shape(value)}")
    for value in onnx_model.graph.output:
        print(f"ONNX output: {value.name} {graph_shape(value)}")
    print("ONNX checker: PASS")


if __name__ == "__main__":
    main()