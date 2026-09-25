#!/usr/bin/env bash

# Stage 4: one live RTSP H.264 source with YOLOv8n and NvDCF tracking.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEEPSTREAM_LIB_DIR="/opt/nvidia/deepstream/deepstream-7.0/lib"
PROBE_BINARY="$PROJECT_ROOT/src/stage4_rtsp_tracking"
CONFIG_TEMPLATE="$PROJECT_ROOT/configs/yolov8n_primary.txt"
LABELS_PATH="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed.engine"
PARSER_LIBRARY="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
TRACKER_LIBRARY="$DEEPSTREAM_LIB_DIR/libnvds_nvmultiobjecttracker.so"
TRACKER_CONFIG="/opt/nvidia/deepstream/deepstream-7.0/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml"

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s <rtsp-uri>\n' "$0" >&2
    exit 2
fi

RTSP_URI="$1"

for command in gst-inspect-1.0 nm sed mktemp; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'MISSING: command: %s\n' "$command" >&2
        exit 1
    fi
done

for path in "$PROBE_BINARY" "$CONFIG_TEMPLATE" "$LABELS_PATH" "$ENGINE_PATH" "$PARSER_LIBRARY" "$TRACKER_LIBRARY" "$TRACKER_CONFIG"; do
    if [ ! -r "$path" ]; then
        printf 'MISSING: readable path: %s\n' "$path" >&2
        exit 1
    fi
done

for element in rtspsrc rtph264depay h264parse nvv4l2decoder nvstreammux nvinfer nvtracker fakesink; do
    if ! LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" gst-inspect-1.0 "$element" >/dev/null 2>&1; then
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

printf 'Stage 4 source: RTSP URI supplied as launcher argument.\n'
printf 'Engine: %s\n' "$ENGINE_PATH"
printf 'Parser: %s\n' "$PARSER_LIBRARY"
printf 'Tracker library: %s\n' "$TRACKER_LIBRARY"
printf 'Tracker config: %s\n' "$TRACKER_CONFIG"
printf 'RTSP properties: protocols=tcp latency=200\n'
printf 'nvstreammux: live-source=1 batch-size=1\n'
printf 'Probe: nvtracker src; at most 20 frames with detections are logged.\n'
printf 'Stop the live pipeline with Ctrl+C; expected exit status is 0.\n'

LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$PROBE_BINARY" "$RTSP_URI" "$RUNTIME_CONFIG" "$LABELS_PATH" \
    "$TRACKER_LIBRARY" "$TRACKER_CONFIG"