#!/usr/bin/env bash

# Stage 2B3C: headless one-source YOLOv8n nvinfer integration smoke test.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEFAULT_VIDEO="/opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.h264"
CONFIG_TEMPLATE="$PROJECT_ROOT/configs/yolov8n_primary.txt"
LABELS_PATH="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed.engine"
PARSER_LIBRARY="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
VIDEO_PATH="${1:-$DEFAULT_VIDEO}"

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [path/to/video.h264]\n' "$0" >&2
    exit 2
fi

for command in gst-launch-1.0 gst-inspect-1.0 nm sed mktemp; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'MISSING: command: %s\n' "$command" >&2
        exit 1
    fi
done

for path in "$VIDEO_PATH" "$CONFIG_TEMPLATE" "$LABELS_PATH" "$ENGINE_PATH" "$PARSER_LIBRARY"; do
    if [ ! -r "$path" ]; then
        printf 'MISSING: readable path: %s\n' "$path" >&2
        exit 1
    fi
done

for element in h264parse nvv4l2decoder nvstreammux nvinfer fakesink; do
    if ! gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'MISSING: GStreamer element: %s\n' "$element" >&2
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

printf 'Stage 2B3C input: %s\n' "$VIDEO_PATH"
printf 'Engine: %s\n' "$ENGINE_PATH"
printf 'Parser: %s\n' "$PARSER_LIBRARY"
printf 'Parser symbol: NvDsInferParseCustomYoloV8\n'
printf 'Pipeline: filesrc -> h264parse -> nvv4l2decoder -> nvstreammux -> nvinfer -> fakesink\n'
printf 'Expected result: existing engine and CPU parser load, then EOS with exit status 0.\n'

gst-launch-1.0 -e \
    filesrc location="$VIDEO_PATH" ! \
    h264parse ! \
    nvv4l2decoder ! \
    "video/x-raw(memory:NVMM)" ! \
    mux.sink_0 \
    nvstreammux name=mux batch-size=1 width=1280 height=720 batched-push-timeout=4000000 ! \
    nvinfer config-file-path="$RUNTIME_CONFIG" ! \
    fakesink sync=false