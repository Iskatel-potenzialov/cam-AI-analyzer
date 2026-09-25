#!/usr/bin/env bash

# Stage 2B3D: read detector metadata from the nvinfer source pad.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_VIDEO="/opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.h264"
PROBE_BINARY="$PROJECT_ROOT/src/stage2b3d_metadata_probe"
CONFIG_TEMPLATE="$PROJECT_ROOT/configs/yolov8n_primary.txt"
LABELS_PATH="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed.engine"
PARSER_LIBRARY="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
DEEPSTREAM_LIB_DIR="/opt/nvidia/deepstream/deepstream-7.0/lib"
VIDEO_PATH="${1:-$DEFAULT_VIDEO}"

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [path/to/video.h264]\n' "$0" >&2
    exit 2
fi

for command in nm sed mktemp; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'MISSING: command: %s\n' "$command" >&2
        exit 1
    fi
done

for path in "$PROBE_BINARY" "$VIDEO_PATH" "$CONFIG_TEMPLATE" "$LABELS_PATH" "$ENGINE_PATH" "$PARSER_LIBRARY"; do
    if [ ! -r "$path" ]; then
        printf 'MISSING: readable path: %s\n' "$path" >&2
        exit 1
    fi
done

if ! nm -D "$PARSER_LIBRARY" | grep -q 'NvDsInferParseCustomYoloV8$'; then
    printf 'MISSING: parser symbol NvDsInferParseCustomYoloV8 in %s\n' "$PARSER_LIBRARY" >&2
    exit 1
fi

RUNTIME_CONFIG="$(mktemp "${TMPDIR:-/tmp}/yolov8n_primary.XXXXXX")"
trap 'rm -f "$RUNTIME_CONFIG"' EXIT
sed "s|@PROJECT_ROOT@|$PROJECT_ROOT|g" "$CONFIG_TEMPLATE" > "$RUNTIME_CONFIG"

if grep -q '@PROJECT_ROOT@' "$RUNTIME_CONFIG"; then
    printf 'ERROR: unresolved project-root placeholder in runtime nvinfer config\n' >&2
    exit 1
fi

printf 'Stage 2B3D input: %s\n' "$VIDEO_PATH"
printf 'Engine: %s\n' "$ENGINE_PATH"
printf 'Parser: %s\n' "$PARSER_LIBRARY"
printf 'Probe: nvinfer src pad; at most 20 frames with detections are logged.\n'
printf 'Expected result: object metadata is printed, then EOS with exit status 0.\n'

LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$PROBE_BINARY" "$VIDEO_PATH" "$RUNTIME_CONFIG" "$LABELS_PATH"