#!/usr/bin/env python3
"""Append the YOLOv8 output transpose required by the NVIDIA parser layout."""

from __future__ import annotations

from pathlib import Path

import onnx
from onnx import TensorProto, helper


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_PATH = PROJECT_ROOT / "models" / "yolov8n" / "artifacts" / "yolov8n.onnx"
TARGET_PATH = PROJECT_ROOT / "models" / "yolov8n" / "artifacts" / "yolov8n_transposed.onnx"
SOURCE_SHAPE = [1, 84, 8400]
TARGET_SHAPE = [1, 8400, 84]


def tensor_shape(value: onnx.ValueInfoProto) -> list[int | str]:
    return [
        dimension.dim_value
        if dimension.HasField("dim_value")
        else dimension.dim_param or "?"
        for dimension in value.type.tensor_type.shape.dim
    ]


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


def main() -> None:
    if not SOURCE_PATH.is_file():
        raise FileNotFoundError(f"MISSING: source ONNX artifact: {SOURCE_PATH}")

    model = onnx.load(SOURCE_PATH)
    if len(model.graph.output) != 1:
        raise ValueError(f"Expected exactly one ONNX output, got {len(model.graph.output)}")

    source_output = model.graph.output[0]
    source_shape = tensor_shape(source_output)
    if source_shape != SOURCE_SHAPE:
        raise ValueError(
            f"Expected source output shape {SOURCE_SHAPE}, got {source_shape}"
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
        TARGET_SHAPE,
    )

    model.graph.node.append(transpose_node)
    del model.graph.output[:]
    model.graph.output.extend([target_output])

    TARGET_PATH.parent.mkdir(parents=True, exist_ok=True)
    onnx.save(model, TARGET_PATH)

    checked_model = onnx.load(TARGET_PATH)
    onnx.checker.check_model(checked_model)
    checked_output_shape = tensor_shape(checked_model.graph.output[0])
    if checked_output_shape != TARGET_SHAPE:
        raise ValueError(
            f"Expected transposed output shape {TARGET_SHAPE}, got {checked_output_shape}"
        )

    for value in checked_model.graph.input:
        print(f"ONNX input: {value.name} {tensor_shape(value)}")
    print(f"Source output: {source_output.name} {source_shape}")
    print(f"Transposed output: {target_name} {checked_output_shape}")
    print(f"ONNX saved: {TARGET_PATH}")
    print("ONNX checker: PASS")


if __name__ == "__main__":
    main()