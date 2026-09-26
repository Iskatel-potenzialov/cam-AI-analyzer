#!/usr/bin/env bash
set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DS_LIB=/opt/nvidia/deepstream/deepstream-7.0/lib
BINARY="$PROJECT_ROOT/src/stage5b_5cam"
CONFIG="$PROJECT_ROOT/configs/yolov8n_primary_b5.txt"
PREPROCESS_CONFIG="$PROJECT_ROOT/configs/stage6a_preprocess_b5.txt"
LABELS="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b5.engine"
PARSER="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
TRACKER="$DS_LIB/libnvds_nvmultiobjecttracker.so"
TRACKER_CONFIG="$PROJECT_ROOT/configs/config_tracker_NvDCF_perf_expD.yml"
LOCAL_SOURCES="$PROJECT_ROOT/config/local_hls_sources.txt"
NO_DISPLAY=0
BENCHMARK=0
LINE_DEBUG=0
QUALITY_DEBUG=0
VEHICLE_LINE_DEBUG=0
VEHICLE_LINE_ZONE_DEBUG=0
VEHICLE_QUALITY_DEBUG=0
while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-display) NO_DISPLAY=1; shift ;;
        --benchmark) BENCHMARK=1; shift ;;
        --line-debug) LINE_DEBUG=1; shift ;;
        --quality-debug) QUALITY_DEBUG=1; shift ;;
        --vehicle-line-debug) VEHICLE_LINE_DEBUG=1; shift ;;
        --vehicle-line-zone-debug) VEHICLE_LINE_ZONE_DEBUG=1; shift ;;
        --vehicle-quality-debug) VEHICLE_QUALITY_DEBUG=1; shift ;;
        *) break ;;
    esac
done

if [ "$#" -eq 0 ]; then
    [ -r "$LOCAL_SOURCES" ] || { printf 'MISSING: readable local HLS source file: %s\n' "$LOCAL_SOURCES" >&2; exit 1; }
    mapfile -t URLS < <(sed -e '/^[[:space:]]*$/d' -e 's/\r$//' "$LOCAL_SOURCES")
elif [ "$#" -eq 5 ]; then
    URLS=("$@")
else
    printf 'Usage: %s [--no-display] [--benchmark] [--line-debug] [--quality-debug] [--vehicle-line-debug] [--vehicle-line-zone-debug] [--vehicle-quality-debug] [<hls-url-0> <hls-url-1> <hls-url-2> <hls-url-3> <hls-url-4>]\n' "$0" >&2
    exit 2
fi

[ "${#URLS[@]}" -eq 5 ] || { printf 'ERROR: exactly 5 non-empty HLS URLs are required.\n' >&2; exit 1; }
for url in "${URLS[@]}"; do
    [[ "$url" == http://* || "$url" == https://* ]] || { printf 'ERROR: every HLS URL must begin with http:// or https://.\n' >&2; exit 1; }
done

for c in gst-inspect-1.0 nm sed grep mktemp; do command -v "$c" >/dev/null || { printf 'MISSING: command: %s\n' "$c" >&2; exit 1; }; done
for p in "$BINARY" "$CONFIG" "$PREPROCESS_CONFIG" "$LABELS" "$ENGINE" "$PARSER" "$TRACKER" "$TRACKER_CONFIG"; do [ -r "$p" ] || { printf 'MISSING: readable path: %s\n' "$p" >&2; exit 1; }; done
SINK_ELEMENT=nveglglessink
DISPLAY_ARGS=()
if [ "$NO_DISPLAY" -eq 1 ]; then
    SINK_ELEMENT=fakesink
    DISPLAY_ARGS=(--no-display)
fi
if [ "$BENCHMARK" -eq 1 ]; then
    DISPLAY_ARGS+=(--benchmark)
fi
if [ "$LINE_DEBUG" -eq 1 ]; then
    DISPLAY_ARGS+=(--line-debug)
fi
if [ "$QUALITY_DEBUG" -eq 1 ]; then
    DISPLAY_ARGS+=(--quality-debug)
fi
if [ "$VEHICLE_LINE_DEBUG" -eq 1 ]; then
    DISPLAY_ARGS+=(--vehicle-line-debug)
fi
if [ "$VEHICLE_LINE_ZONE_DEBUG" -eq 1 ]; then
    DISPLAY_ARGS+=(--vehicle-line-zone-debug)
fi
if [ "$VEHICLE_QUALITY_DEBUG" -eq 1 ]; then
    DISPLAY_ARGS+=(--vehicle-quality-debug)
fi
for x in souphttpsrc hlsdemux tsdemux h264parse nvv4l2decoder nvstreammux nvdspreprocess nvinfer nvtracker nvmultistreamtiler nvvideoconvert nvdsosd "$SINK_ELEMENT"; do LD_LIBRARY_PATH="$DS_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" gst-inspect-1.0 "$x" >/dev/null 2>&1 || { printf 'MISSING: GStreamer element: %s\n' "$x" >&2; exit 1; }; done
[ "$(grep -c '^roi-params-src-0=1250;616;1140;824$' "$PREPROCESS_CONFIG")" -eq 1 ] || { printf 'ERROR: Stage 7 preprocess ROI must be 1250;616;1140;824.\n' >&2; exit 1; }
nm -D "$PARSER" | grep -q 'NvDsInferParseCustomYoloV8$' || { printf 'MISSING: parser symbol\n' >&2; exit 1; }
RUNTIME_CONFIG="$(mktemp "${TMPDIR:-/tmp}/yolov8n_primary_b5.XXXXXX")"; trap 'rm -f "$RUNTIME_CONFIG"' EXIT; sed "s|@PROJECT_ROOT@|$PROJECT_ROOT|g" "$CONFIG" > "$RUNTIME_CONFIG"
printf 'Stage 5B: five HLS URLs loaded without printing them.\nMux: live-source=1 batch-size=5.\nEngine: %s\nDisplay: tiler rows=2 columns=3; cells=640x360.\n' "$ENGINE"
if [ "$NO_DISPLAY" -eq 1 ]; then
    printf 'Display control mode: fakesink after nvdsosd.\n'
fi
if [ "$BENCHMARK" -eq 1 ]; then
    printf 'Benchmark mode: source FPS and pipeline output batches/s every 5 seconds.\n'
fi
if [ "$LINE_DEBUG" -eq 1 ]; then
    printf 'Line debug mode: source 0 person transitions only.\n'
fi
if [ "$QUALITY_DEBUG" -eq 1 ]; then
    printf 'Quality debug mode: source 0 person tracks only.\n'
fi
if [ "$VEHICLE_LINE_DEBUG" -eq 1 ]; then
    printf 'Vehicle line debug mode: source 0 car, motorcycle, and bus transitions only.\n'
fi
if [ "$VEHICLE_LINE_ZONE_DEBUG" -eq 1 ]; then
    printf 'Vehicle line zone debug: source 0 shows the exact infinite-line near zones for GREEN and BLUE within the vehicle ROI.\n'
fi
if [ "$VEHICLE_QUALITY_DEBUG" -eq 1 ]; then
    printf 'Vehicle quality debug mode: source 0 car, motorcycle, and bus diagnostics only.\n'
fi
LD_LIBRARY_PATH="$DS_LIB${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" exec "$BINARY" "${DISPLAY_ARGS[@]}" "${URLS[@]}" "$RUNTIME_CONFIG" "$LABELS" "$TRACKER" "$TRACKER_CONFIG" "$PREPROCESS_CONFIG"