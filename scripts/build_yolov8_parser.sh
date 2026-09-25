#!/usr/bin/env bash

# Build the project-local CPU YOLOv8 DeepStream custom parser library.

set -eu

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PARSER_DIR="$PROJECT_ROOT/parsers/yolov8"
SOURCE_PATH="$PARSER_DIR/nvdsparsebbox_Yolo.cpp"
LIBRARY_PATH="$PARSER_DIR/libnvdsinfer_custom_impl_Yolo.so"
DEFAULT_DEEPSTREAM_INCLUDE_DIR="/opt/nvidia/deepstream/deepstream-7.0/sources/includes"
DEEPSTREAM_INCLUDE_DIR="${DEEPSTREAM_INCLUDE_DIR:-$DEFAULT_DEEPSTREAM_INCLUDE_DIR}"

if ! command -v make >/dev/null 2>&1; then
    printf 'MISSING: make\n' >&2
    exit 1
fi

if ! command -v nm >/dev/null 2>&1; then
    printf 'MISSING: nm\n' >&2
    exit 1
fi

if [ ! -r "$SOURCE_PATH" ]; then
    printf 'MISSING: parser source: %s\n' "$SOURCE_PATH" >&2
    exit 1
fi

if [ ! -r "$DEEPSTREAM_INCLUDE_DIR/nvdsinfer_custom_impl.h" ]; then
    printf 'MISSING: DeepStream custom-parser header: %s/nvdsinfer_custom_impl.h\n' \
        "$DEEPSTREAM_INCLUDE_DIR" >&2
    exit 1
fi

printf 'Parser source: %s\n' "$SOURCE_PATH"
printf 'DeepStream include directory: %s\n' "$DEEPSTREAM_INCLUDE_DIR"
make -C "$PARSER_DIR" DEEPSTREAM_INCLUDE_DIR="$DEEPSTREAM_INCLUDE_DIR"

if [ ! -s "$LIBRARY_PATH" ]; then
    printf 'ERROR: build completed without creating a non-empty library: %s\n' "$LIBRARY_PATH" >&2
    exit 1
fi

printf 'Library created: %s\n' "$LIBRARY_PATH"
printf 'Library size: %s bytes\n' "$(wc -c < "$LIBRARY_PATH")"
printf 'Exported YOLOv8 parser symbol:\n'
nm -D "$LIBRARY_PATH" | grep 'NvDsInferParseCustomYoloV8$'