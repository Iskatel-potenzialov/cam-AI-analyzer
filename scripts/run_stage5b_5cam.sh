#!/usr/bin/env bash
set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DS_LIB=/opt/nvidia/deepstream/deepstream-7.0/lib
BINARY="$PROJECT_ROOT/src/stage5b_5cam"
CONFIG="$PROJECT_ROOT/configs/yolov8n_primary_b5.txt"
LABELS="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b5.engine"
PARSER="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
TRACKER="$DS_LIB/libnvds_nvmultiobjecttracker.so"
TRACKER_CONFIG=/opt/nvidia/deepstream/deepstream-7.0/samples/configs/deepstream-app/config_tracker_NvDCF_perf.yml
LOCAL_SOURCES="$PROJECT_ROOT/config/local_hls_sources.txt"
NO_DISPLAY=0
if [ "${1:-}" = "--no-display" ]; then
    NO_DISPLAY=1
    shift
fi

if [ "$#" -eq 0 ]; then
    [ -r "$LOCAL_SOURCES" ] || { printf 'MISSING: readable local HLS source file: %s\n' "$LOCAL_SOURCES" >&2; exit 1; }
    mapfile -t URLS < <(sed -e '/^[[:space:]]*$/d' -e 's/\r$//' "$LOCAL_SOURCES")
elif [ "$#" -eq 5 ]; then
    URLS=("$@")
else
    printf 'Usage: %s [--no-display] [<hls-url-0> <hls-url-1> <hls-url-2> <hls-url-3> <hls-url-4>]\n' "$0" >&2
    exit 2
fi

[ "${#URLS[@]}" -eq 5 ] || { printf 'ERROR: exactly 5 non-empty HLS URLs are required.\n' >&2; exit 1; }
for url in "${URLS[@]}"; do
    [[ "$url" == http://* || "$url" == https://* ]] || { printf 'ERROR: every HLS URL must begin with http:// or https://.\n' >&2; exit 1; }
done

for c in gst-inspect-1.0 nm sed mktemp; do command -v "$c" >/dev/null || { printf 'MISSING: command: %s\n' "$c" >&2; exit 1; }; done
for p in "$BINARY" "$CONFIG" "$LABELS" "$ENGINE" "$PARSER" "$TRACKER" "$TRACKER_CONFIG"; do [ -r "$p" ] || { printf 'MISSING: readable path: %s\n' "$p" >&2; exit 1; }; done
SINK_ELEMENT=nveglglessink
DISPLAY_ARGS=()
if [ "$NO_DISPLAY" -eq 1 ]; then
    SINK_ELEMENT=fakesink
    DISPLAY_ARGS=(--no-display)
fi
for x in souphttpsrc hlsdemux tsdemux h264parse nvv4l2decoder nvstreammux nvinfer nvtracker nvmultistreamtiler nvvideoconvert nvdsosd "$SINK_ELEMENT"; do LD_LIBRARY_PATH="$DS_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" gst-inspect-1.0 "$x" >/dev/null 2>&1 || { printf 'MISSING: GStreamer element: %s\n' "$x" >&2; exit 1; }; done
nm -D "$PARSER" | grep -q 'NvDsInferParseCustomYoloV8$' || { printf 'MISSING: parser symbol\n' >&2; exit 1; }
RUNTIME_CONFIG="$(mktemp "${TMPDIR:-/tmp}/yolov8n_primary_b5.XXXXXX")"; trap 'rm -f "$RUNTIME_CONFIG"' EXIT; sed "s|@PROJECT_ROOT@|$PROJECT_ROOT|g" "$CONFIG" > "$RUNTIME_CONFIG"
printf 'Stage 5B: five HLS URLs loaded without printing them.\nMux: live-source=1 batch-size=5.\nEngine: %s\nDisplay: tiler rows=2 columns=3; cells=640x360.\n' "$ENGINE"
if [ "$NO_DISPLAY" -eq 1 ]; then
    printf 'Display control mode: fakesink after nvdsosd.\n'
fi
LD_LIBRARY_PATH="$DS_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec "$BINARY" "${DISPLAY_ARGS[@]}" "${URLS[@]}" "$RUNTIME_CONFIG" "$LABELS" "$TRACKER" "$TRACKER_CONFIG"