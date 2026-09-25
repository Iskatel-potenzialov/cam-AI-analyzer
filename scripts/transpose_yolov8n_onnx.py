#!/usr/bin/env python3
"""Append the YOLOv8 output transpose required by the NVIDIA parser layout."""

from __future__ import annotations

import argparse
from pathlib import Path

import onnx
from onnx import TensorProto, helper


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ARTIFACTS_DIR = PROJECT_ROOT / "models" / "yolov8n" / "artifacts"


def tensor_shape(value: onnx.ValueInfoProto) -> list[int | str]:
    return [
        dimension.dim_value
        if dimension.HasField("dim_value")
        else dimension.dim_param or "?"
        for dimension in value.type.tensor_type.shape.dim
    ]


def artifact_paths_for_batch(batch: int) -> tuple[Path, Path]:
    if batch == 1:
        return (
            ARTIFACTS_DIR / "yolov8n.onnx",
            ARTIFACTS_DIR / "yolov8n_transposed.onnx",
        )
    return (
        ARTIFACTS_DIR / f"yolov8n_b{batch}.onnx",
        ARTIFACTS_DIR / f"yolov8n_b{batch}_transposed.onnx",
    )


def unique_tensor_name(model: onnx.ModelProto, base_name: str) -> str:
    names = {value.name for value in model.graph.input}
    names.update(value.name for value in model.graph.output)
    names.update(value.name for value in model.graph.value_info)
    names.update(value.name for value in model.graph.initializer)
    for node in model.graph.node:
        names.update(node.output)

    candidate = base_name
    suffix = 1
    while candidate in names:
        candidate = f"{base_name}_{suffix}"
        suffix += 1
    return candidate


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--batch",
        type=int,
        choices=(1, 2),
        default=1,
        help="Static ONNX batch size; default preserves the existing batch-1 workflow.",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    batch = args.batch
    source_path, target_path = artifact_paths_for_batch(batch)
    source_shape = [batch, 84, 8400]
    target_shape = [batch, 8400, 84]

    if not source_path.is_file():
        raise FileNotFoundError(f"MISSING: source ONNX artifact: {source_path}")

    model = onnx.load(source_path)
    if len(model.graph.output) != 1:
        raise ValueError(f"Expected exactly one ONNX output, got {len(model.graph.output)}")

    source_output = model.graph.output[0]
    actual_source_shape = tensor_shape(source_output)
    if actual_source_shape != source_shape:
        raise ValueError(
            f"Expected source output shape {source_shape}, got {actual_source_shape}"
        )
    if source_output.type.tensor_type.elem_type == TensorProto.UNDEFINED:
        raise ValueError("Source ONNX output has no tensor element type")

    target_name = unique_tensor_name(model, f"{source_output.name}_transposed")
    transpose_node = helper.make_node(
        "Transpose",
        inputs=[source_output.name],
        outputs=[target_name],
        name="transpose_yolov8_output",
        perm=[0, 2, 1],
    )
    target_output = helper.make_tensor_value_info(
        target_name,
        source_output.type.tensor_type.elem_type,
        target_shape,
    )

    model.graph.node.append(transpose_node)
    del model.graph.output[:]
    model.graph.output.extend([target_output])

    target_path.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, target_path)

    checked_model = onnx.load(target_path)
    onnx.checker.check_model(checked_model)
    checked_input_shapes = [tensor_shape(value) for value in checked_model.graph.input]
    checked_output_shape = tensor_shape(checked_model.graph.output[0])
    expected_input_shape = [batch, 3, 640, 640]
    if checked_input_shapes != [expected_input_shape]:
        raise ValueError(
            f"Expected input shape {expected_input_shape}, got {checked_input_shapes}"
        )
    if checked_output_shape != target_shape:
        raise ValueError(
            f"Expected transposed output shape {target_shape}, got {checked_output_shape}"
        )

    print(f"Static batch: {batch}")
    for value in checked_model.graph.input:
        print(f"ONNX input: {value.name} {tensor_shape(value)}")
    print(f"Source output: {source_output.name} {actual_source_shape}")
    print(f"Transposed output: {target_name} {checked_output_shape}")
    print(f"ONNX saved: {target_path}")
    print("ONNX checker: PASS")


if __name__ == "__main__":
    main()