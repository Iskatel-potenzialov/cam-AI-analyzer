#!/usr/bin/env bash

# Stage 2A: one local H.264 file through the DeepStream sample nvinfer model.
# Run with: bash scripts/run_stage2a_nvinfer.sh [path/to/video.h264]

set -u

DEFAULT_VIDEO="/opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.h264"
NVINFER_CONFIG="/opt/nvidia/deepstream/deepstream-7.0/samples/configs/deepstream-app/config_infer_primary.txt"
VIDEO_PATH="${1:-$DEFAULT_VIDEO}"

if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [path/to/video.h264]\n' "$0" >&2
    exit 2
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    printf 'MISSING: gst-launch-1.0\n' >&2
    exit 1
fi

if [ ! -r "$VIDEO_PATH" ]; then
    printf 'MISSING: readable local H.264 video file: %s\n' "$VIDEO_PATH" >&2
    exit 1
fi

if [ ! -r "$NVINFER_CONFIG" ]; then
    printf 'MISSING: readable nvinfer config: %s\n' "$NVINFER_CONFIG" >&2
    exit 1
fi

for element in h264parse nvv4l2decoder nvstreammux nvinfer fakesink; do
    if ! gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'MISSING: GStreamer element: %s\n' "$element" >&2
        exit 1
    fi
done

printf 'Stage 2A input: %s\n' "$VIDEO_PATH"
printf 'nvinfer config: %s\n' "$NVINFER_CONFIG"
printf 'Pipeline: filesrc -> h264parse -> nvv4l2decoder -> nvstreammux -> nvinfer -> fakesink\n'
printf 'Expected result: nvinfer loads the sample model, inference runs, then EOS and exit status 0.\n'

gst-launch-1.0 -e \
    filesrc location="$VIDEO_PATH" ! \
    h264parse ! \
    nvv4l2decoder ! \
    "video/x-raw(memory:NVMM)" ! \
    mux.sink_0 \
    nvstreammux name=mux batch-size=1 width=1280 height=720 batched-push-timeout=4000000 ! \
    nvinfer config-file-path="$NVINFER_CONFIG" ! \
    fakesink sync=false