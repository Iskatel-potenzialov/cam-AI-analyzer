#!/usr/bin/env bash
set -eu
: "${LD_LIBRARY_PATH:=}"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCHMARK_ROOT="$PROJECT_ROOT/benchmark"
DS_LIB=/opt/nvidia/deepstream/deepstream-7.0/lib
BINARY="$PROJECT_ROOT/src/tracker_benchmark"
BASELINE_TRACKER="$PROJECT_ROOT/configs/config_tracker_NvDCF_perf_expD.yml"
PREPROCESS_CONFIG="$PROJECT_ROOT/configs/stage6a_preprocess_b5.txt"
NFINFER_CONFIG="$PROJECT_ROOT/configs/yolov8n_primary_b5.txt"
LABELS="$PROJECT_ROOT/configs/coco_labels.txt"
ENGINE="$PROJECT_ROOT/models/yolov8n/artifacts/yolov8n_fp16_transposed_b5.engine"
PARSER="$PROJECT_ROOT/parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so"
TRACKER="$DS_LIB/libnvds_nvmultiobjecttracker.so"

if [ "$#" -ne 1 ]; then
    printf 'Usage: %s <source1-h264-clip.mp4>\n' "$0" >&2
    exit 2
fi
CLIP="$1"
[ -s "$CLIP" ] || { printf 'ERROR: readable non-empty clip required: %s\n' "$CLIP" >&2; exit 1; }

for command in ffmpeg ffprobe gst-inspect-1.0 nm sed grep mktemp date sha256sum cp awk; do
    command -v "$command" >/dev/null || { printf 'MISSING: command: %s\n' "$command" >&2; exit 1; }
done
CODEC="$(ffprobe -v error -select_streams v:0 -show_entries stream=codec_name -of default=nokey=1:noprint_wrappers=1 "$CLIP")"
[ "$CODEC" = h264 ] || { printf 'ERROR: benchmark clip must contain H.264 video; found: %s\n' "$CODEC" >&2; exit 1; }
ffmpeg -hide_banner -encoders 2>/dev/null | grep -q 'libx264' || { printf 'MISSING: ffmpeg encoder libx264 (required for pairwise comparison videos).\n' >&2; exit 1; }
for element in filesrc qtdemux h264parse nvv4l2decoder nvstreammux nvdspreprocess nvinfer nvtracker nvstreamdemux queue nvvideoconvert capsfilter nvdsosd nvv4l2h264enc qtmux filesink; do
    LD_LIBRARY_PATH="$DS_LIB:$LD_LIBRARY_PATH" gst-inspect-1.0 "$element" >/dev/null 2>&1 || { printf 'MISSING: GStreamer element: %s\n' "$element" >&2; exit 1; }
done
for path in "$BINARY" "$BASELINE_TRACKER" "$PREPROCESS_CONFIG" "$NFINFER_CONFIG" "$LABELS" "$ENGINE" "$PARSER" "$TRACKER" "$BENCHMARK_ROOT/configs/nvdcf_B.yml" "$BENCHMARK_ROOT/configs/nvdcf_C.yml" "$BENCHMARK_ROOT/configs/nvdcf_D.yml"; do
    [ -r "$path" ] || { printf 'MISSING: readable path: %s\n' "$path" >&2; exit 1; }
done
grep -qx 'roi-params-src-1=1034;586;1286;850' "$PREPROCESS_CONFIG" || { printf 'ERROR: source1 production ROI is not 1034;586;1286;850.\n' >&2; exit 1; }
grep -qx 'network-input-shape=5;3;640;640' "$PREPROCESS_CONFIG" || { printf 'ERROR: expected production batch-5 640x640 preprocessing.\n' >&2; exit 1; }
grep -qx 'batch-size=5' "$NFINFER_CONFIG" || { printf 'ERROR: expected production batch-5 nvinfer config.\n' >&2; exit 1; }
grep -qx 'pre-cluster-threshold=0.235' "$NFINFER_CONFIG" || { printf 'ERROR: expected production detector threshold 0.235.\n' >&2; exit 1; }
nm -D "$PARSER" | grep -q 'NvDsInferParseCustomYoloV8$' || { printf 'MISSING: parser symbol NvDsInferParseCustomYoloV8.\n' >&2; exit 1; }

mkdir -p "$BENCHMARK_ROOT/results"
SESSION="$(mktemp -d "$BENCHMARK_ROOT/results/$(date -u +%Y-%m-%dT%H-%M-%SZ)_XXXXXX")"
mkdir -p "$SESSION/configs"
RUNTIME_CONFIG="$(mktemp "/tmp/yolov8n_primary_b5.XXXXXX")"
trap 'rm -f "$RUNTIME_CONFIG"' EXIT
sed "s|@PROJECT_ROOT@|$PROJECT_ROOT|g" "$NFINFER_CONFIG" > "$RUNTIME_CONFIG"

write_overlay_text() {
    local variant="$1"
    local config="$2"
    local output="$3"
    awk -v variant="$variant" '
BEGIN {
    print variant
    order[1] = "probationAge"
    order[2] = "maxShadowTrackingAge"
    order[3] = "earlyTerminationAge"
    order[4] = "minTrackerConfidence"
    order[5] = "minDetectorConfidence"
    order[6] = "useReid"
    order[7] = "reidType"
    order[8] = "matchingScoreWeight4Iou"
    order[9] = "matchingScoreWeight4VisualSimilarity"
    order[10] = "matchingScoreWeight4SizeSimilarity"
    order[11] = "matchingScoreWeight4Age"
    for (i = 1; i <= 11; ++i) wanted[order[i]] = 1
}
{
    line = $0
    sub(/#.*/, "", line)
    separator = index(line, ":")
    if (separator > 0) {
        key = substr(line, 1, separator - 1)
        parsed_value = substr(line, separator + 1)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", key)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", parsed_value)
        if (wanted[key] && parsed_value != "") value[key] = parsed_value
    }
}
END {
    shown = 0
    for (i = 1; i <= 11 && shown < 8; ++i) {
        key = order[i]
        if (key in value) {
            print key ": " value[key]
            ++shown
        }
    }
}' "$config" > "$output"
}

run_variant() {
    local variant="$1"
    local tracker_config="$2"
    local output="$SESSION/$variant.mp4"
    local used_config="$SESSION/configs/nvdcf_$variant.yml"
    local overlay="$SESSION/configs/nvdcf_$variant.overlay.txt"
    cp "$tracker_config" "$used_config"
    write_overlay_text "$variant" "$used_config" "$overlay"
    printf 'TRACKER_BENCHMARK_RUN variant=%s tracker_config=%s\n' "$variant" "$tracker_config"
    LD_LIBRARY_PATH="$DS_LIB:$LD_LIBRARY_PATH" \
        "$BINARY" "$CLIP" "$output" "$variant" "$RUNTIME_CONFIG" "$LABELS" \
        "$TRACKER" "$used_config" "$PREPROCESS_CONFIG" "$overlay"
    [ -s "$output" ] || { printf 'ERROR: benchmark did not produce %s\n' "$output" >&2; exit 1; }
}

run_variant A "$BASELINE_TRACKER"
run_variant B "$BENCHMARK_ROOT/configs/nvdcf_B.yml"
run_variant C "$BENCHMARK_ROOT/configs/nvdcf_C.yml"
run_variant D "$BENCHMARK_ROOT/configs/nvdcf_D.yml"

cp "$PREPROCESS_CONFIG" "$SESSION/configs/stage6a_preprocess_b5.txt"
cp "$NFINFER_CONFIG" "$SESSION/configs/yolov8n_primary_b5.txt"
{
    printf 'clip=%s\n' "$CLIP"
    printf 'clip_sha256=%s\n' "$(sha256sum "$CLIP" | awk '{print $1}')"
    printf 'clip_video_codec=%s\n' "$CODEC"
    printf 'source_id=1\nsource1_roi=1034,586,1286,850\n'
    printf 'detector_config=%s\npreprocess_config=%s\nproduction_tracker_baseline=%s\n' "$NFINFER_CONFIG" "$PREPROCESS_CONFIG" "$BASELINE_TRACKER"
    printf 'comparison_mode=pairwise_vertical\npairwise_resolution=2560x2880\n'
    sha256sum "$SESSION/configs"/*.yml "$SESSION/configs"/*.txt
} > "$SESSION/run_info.txt"

make_pairwise() {
    local lower_variant="$1"
    local output="$SESSION/A_vs_$lower_variant.mp4"
    ffmpeg -hide_banner -y -i "$SESSION/A.mp4" -i "$SESSION/$lower_variant.mp4" \
        -filter_complex '[0:v][1:v]vstack=inputs=2,format=yuv420p[v]' \
        -map '[v]' -an -shortest -c:v libx264 -preset medium -crf 18 -movflags +faststart "$output"
    [ -s "$output" ] || { printf 'ERROR: pairwise comparison was not produced: %s\n' "$output" >&2; exit 1; }
    dimensions="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "$output")"
    [ "$dimensions" = "2560x2880" ] || { printf 'ERROR: expected 2560x2880 pairwise video, got %s: %s\n' "$dimensions" "$output" >&2; exit 1; }
}

make_pairwise B
make_pairwise C
make_pairwise D

printf 'TRACKER_BENCHMARK_COMPLETE session=%s\nA=%s\nB=%s\nC=%s\nD=%s\nA_vs_B=%s\nA_vs_C=%s\nA_vs_D=%s\n' \
    "$SESSION" "$SESSION/A.mp4" "$SESSION/B.mp4" "$SESSION/C.mp4" "$SESSION/D.mp4" \
    "$SESSION/A_vs_B.mp4" "$SESSION/A_vs_C.mp4" "$SESSION/A_vs_D.mp4"
