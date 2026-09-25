#!/usr/bin/env bash

# Stage 5A.1: one HLS source through GStreamer HLS demux and NVIDIA H.264 decode.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEEPSTREAM_LIB_DIR="/opt/nvidia/deepstream/deepstream-7.0/lib"
PROBE_BINARY="$PROJECT_ROOT/src/stage5a1_hls_decode"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    printf 'Usage: %s <hls-url> [duration-seconds]\n' "$0" >&2
    exit 2
fi

HLS_URL="$1"
DURATION="${2:-}"

if [ -n "$DURATION" ] && ! [[ "$DURATION" =~ ^[1-9][0-9]*$ ]]; then
    printf 'ERROR: duration-seconds must be a positive integer\n' >&2
    exit 2
fi

for command in gst-inspect-1.0; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'MISSING: command: %s\n' "$command" >&2
        exit 1
    fi
done

if [ ! -x "$PROBE_BINARY" ]; then
    printf 'MISSING: executable: %s\n' "$PROBE_BINARY" >&2
    printf 'Build it first: make -C %s stage5a1_hls_decode\n' "$PROJECT_ROOT/src" >&2
    exit 1
fi

for element in souphttpsrc hlsdemux tsdemux h264parse nvv4l2decoder fakesink; do
    if ! LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        gst-inspect-1.0 "$element" >/dev/null 2>&1; then
        printf 'MISSING: GStreamer element: %s\n' "$element" >&2
        exit 1
    fi
done

printf 'Stage 5A.1: HLS URL accepted as a runtime argument (not printed).\n'
printf 'HTTP headers: User-Agent and Referer are configured in the utility.\n'
if [ -n "$DURATION" ]; then
    printf 'Requested duration: %s seconds.\n' "$DURATION"
else
    printf 'Requested duration: until Ctrl+C.\n'
fi
printf 'Pipeline: souphttpsrc -> hlsdemux -> tsdemux -> h264parse -> nvv4l2decoder -> fakesink\n'

if [ -n "$DURATION" ]; then
    LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
        exec "$PROBE_BINARY" "$HLS_URL" "$DURATION"
fi

LD_LIBRARY_PATH="$DEEPSTREAM_LIB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$PROBE_BINARY" "$HLS_URL"