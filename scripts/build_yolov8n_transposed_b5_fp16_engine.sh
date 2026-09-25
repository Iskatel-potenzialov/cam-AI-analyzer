#!/usr/bin/env bash
set -eu
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TRTEXEC=/usr/src/tensorrt/bin/trtexec
ONNX_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_b5_transposed.onnx"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b5.engine"
[ -x "$TRTEXEC" ] || { printf 'MISSING: executable trtexec: %s\n' "$TRTEXEC" >&2; exit 1; }
[ -r "$ONNX_PATH" ] || { printf 'MISSING: readable batch-5 transposed ONNX artifact: %s\n' "$ONNX_PATH" >&2; exit 1; }
mkdir -p "$(dirname "$ENGINE_PATH")"
printf 'ONNX: %s\nEngine: %s\nTensorRT: FP16, GPU 0, static input shape [5,3,640,640]\nExpected output shape: [5,8400,84]\n' "$ONNX_PATH" "$ENGINE_PATH"
"$TRTEXEC" --onnx="$ONNX_PATH" --saveEngine="$ENGINE_PATH" --fp16 --device=0
[ -s "$ENGINE_PATH" ] || { printf 'ERROR: no engine created\n' >&2; exit 1; }
printf 'Engine created: %s\nEngine size: %s bytes\n' "$ENGINE_PATH" "$(wc -c < "$ENGINE_PATH")"