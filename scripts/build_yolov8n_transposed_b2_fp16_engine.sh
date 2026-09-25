#!/usr/bin/env bash

# Stage 5A.0: build a static batch-2 FP16 TensorRT engine from transposed YOLOv8n ONNX.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRTEXEC="/usr/src/tensorrt/bin/trtexec"
ONNX_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_b2_transposed.onnx"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b2.engine"

if [ ! -x "$TRTEXEC" ]; then
    printf 'MISSING: executable trtexec: %s\n' "$TRTEXEC" >&2
    exit 1
fi

if [ ! -r "$ONNX_PATH" ]; then
    printf 'MISSING: readable batch-2 transposed ONNX artifact: %s\n' "$ONNX_PATH" >&2
    exit 1
fi

mkdir -p "$(dirname "$ENGINE_PATH")"

printf 'ONNX: %s\n' "$ONNX_PATH"
printf 'Engine: %s\n' "$ENGINE_PATH"
printf 'TensorRT: FP16, GPU 0, static input shape [2,3,640,640]\n'
printf 'Expected output shape: [2,8400,84]\n'

"$TRTEXEC" \
    --onnx="$ONNX_PATH" \
    --saveEngine="$ENGINE_PATH" \
    --fp16 \
    --device=0

if [ ! -s "$ENGINE_PATH" ]; then
    printf 'ERROR: trtexec completed without creating a non-empty engine: %s\n' "$ENGINE_PATH" >&2
    exit 1
fi

printf 'Engine created: %s\n' "$ENGINE_PATH"
printf 'Engine size: %s bytes\n' "$(wc -c < "$ENGINE_PATH")"