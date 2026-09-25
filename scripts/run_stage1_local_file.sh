#!/usr/bin/env bash

# Stage 1: one local H.264 file through DeepStream elements, headlessly.
# Run with: bash scripts/run_stage1_local_file.sh [path/to/video.h264]

set -u

DEFAULT_VIDEO="/opt/nvidia/deepstream/deepstream/samples/streams/sample_720p.h264"
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

for element in nvv4l2decoder nvstreammux fakesink; do
    if ! gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'MISSING: GStreamer element: %s\n' "$element" >&2
        exit 1
    fi
done

printf 'Stage 1 input: %s\n' "$VIDEO_PATH"
printf 'Pipeline: filesrc -> h264parse -> nvv4l2decoder -> nvstreammux -> fakesink\n'
printf 'Expected result: EOS followed by a zero exit status.\n'

gst-launch-1.0 -e \
    filesrc location="$VIDEO_PATH" ! \
    h264parse ! \
    nvv4l2decoder ! \
    "video/x-raw(memory:NVMM)" ! \
    mux.sink_0 \
    nvstreammux name=mux batch-size=1 width=1280 height=720 batched-push-timeout=4000000 ! \
    fakesink sync=false