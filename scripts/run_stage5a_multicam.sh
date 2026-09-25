#!/usr/bin/env bash

# Stage 5A: one RTSP and one HLS source in a shared batch-2 DeepStream pipeline.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEEPSTREAM_LIB_DIR="/opt/nvidia/deepstream/deepstream-7.0/lib"
BINARY="$PROJECT_ROOT/src/stage5a_multicam"
CONFIG_TEMPLATE="$PROJECT_ROOT/configs/yolov8n_primary_b2.txt"
LABELS_PATH="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE_PATH="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b2.engine"
PARSER_LIBRARY="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
TRACKER_LIBRARY="$DEEPSTREAM_LIB_DIR/libnvds_nvmultiobjecttracker.so"
TRACKER_CONFIG="/opt/nvidia/deepstream/deepstream-7.0/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml"

if [ "$#" -ne 2 ]; then
    printf 'Usage: %s <rtsp-url> <hls-url>\n' "$0" >&2
    exit 2
fi

RTSP_URL="$1"
HLS_URL="$2"

for command in gst-inspect-1.0 nm sed mktemp; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'MISSING: command: %s\n' "$command" >&2
        exit 1
    fi
done

for path in "$BINARY" "$CONFIG_TEMPLATE" "$LABELS_PATH" "$ENGINE_PATH" "$PARSER_LIBRARY" "$TRACKER_LIBRARY" "$TRACKER_CONFIG"; do
    if [ ! -r "$path" ]; then
        printf 'MISSING: readable path: %s\n' "$path" >&2
        exit 1
    fi
done

for element in rtspsrc rtph264depay souphttpsrc hlsdemux tsdemux h264parse nvv4l2decoder nvstreammux nvinfer nvtracker nvmultistreamtiler nvvideoconvert nvdsosd nveglglessink; do
    if ! LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'MISSING: GStreamer element: %s\n' "$element" >&2
        exit 1
    fi
done

if ! nm -D "$PARSER_LIBRARY" | grep -q 'NvDsInferParseCustomYoloV8$'; then
    printf 'MISSING: parser symbol NvDsInferParseCustomYoloV8 in %s\n' "$PARSER_LIBRARY" >&2
    exit 1
fi

RUNTIME_CONFIG="$(mktemp "${TMPDIR:-/tmp}/yolov8n_primary_b2.XXXXXX")"
trap 'rm -f "$RUNTIME_CONFIG"' EXIT
sed "s|@PROJECT_ROOT@|$PROJECT_ROOT|g" "$CONFIG_TEMPLATE" > "$RUNTIME_CONFIG"
if grep -q '@PROJECT_ROOT@' "$RUNTIME_CONFIG"; then
    printf 'ERROR: unresolved project-root placeholder in runtime nvinfer config\n' >&2
    exit 1
fi

printf 'Stage 5A: RTSP and HLS URLs accepted as runtime arguments (not printed).\n'
printf 'RTSP: protocols=tcp latency=200.\n'
printf 'HLS: souphttpsrc User-Agent and Referer configured.\n'
printf 'Mux: live-source=1 batch-size=2.\n'
printf 'Engine: %s\n' "$ENGINE_PATH"
printf 'Tracker: %s\n' "$TRACKER_CONFIG"
printf 'Display: tiler rows=1 columns=2; caps=video/x-raw(memory:NVMM),format=RGBA.\n'
printf 'Stop with Ctrl+C; expected exit status is 0.\n'

LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$BINARY" "$RTSP_URL" "$HLS_URL" "$RUNTIME_CONFIG" "$LABELS_PATH" \
    "$TRACKER_LIBRARY" "$TRACKER_CONFIG"