#!/usr/bin/env bash

# Read-only Ubuntu runtime diagnostic for DeepStream Car Tracking Stage 0.
# Run after activating the intended Python environment, if applicable.

section() {
    printf '\n=== %s ===\n' "$1"
}

missing() {
    printf 'MISSING: %s\n' "$1"
}

warning() {
    printf 'WARNING: %s\n' "$1"
}

command_version() {
    local label="$1"
    shift

    if command -v "$1" >/dev/null 2>&1; then
        printf '%s:\n' "$label"
        "$@" 2>&1 || warning "${label} command returned a non-zero exit status"
    else
        missing "command not found: $1"
    fi
}

check_gstreamer_plugin() {
    local plugin="$1"

    if ! command -v gst-inspect-1.0 >/dev/null 2>&1; then
        missing "cannot check ${plugin}; gst-inspect-1.0 is unavailable"
        return
    fi

    if gst-inspect-1.0 "$plugin" >/dev/null 2>&1; then
        printf 'OK: %s\n' "$plugin"
    else
        missing "GStreamer plugin: ${plugin}"
    fi
}

section "System"
printf 'Timestamp: '
date -Is 2>/dev/null || date
printf 'Hostname: '
hostname 2>/dev/null || missing "hostname command failed"
printf 'Kernel: '
uname -srmo 2>/dev/null || missing "uname command failed"

if command -v lsb_release >/dev/null 2>&1; then
    printf 'Ubuntu release: '
    lsb_release -ds 2>/dev/null || warning "lsb_release failed"
elif [ -r /etc/os-release ]; then
    printf 'Ubuntu release: '
    . /etc/os-release
    printf '%s\n' "${PRETTY_NAME:-unknown}"
else
    missing "Ubuntu release information"
fi

section "NVIDIA GPU and CUDA visibility"
if command -v nvidia-smi >/dev/null 2>&1; then
    printf 'nvidia-smi path: %s\n' "$(command -v nvidia-smi)"
    printf 'nvidia-smi output:\n'
    nvidia-smi 2>&1 || warning "nvidia-smi returned a non-zero exit status"
    printf 'GPU name and NVIDIA driver version:\n'
    nvidia-smi --query-gpu=name,driver_version --format=csv,noheader 2>&1 || \
        warning "unable to query GPU name and driver version"
else
    missing "nvidia-smi"
fi

printf 'CUDA_VISIBLE_DEVICES: %s\n' "${CUDA_VISIBLE_DEVICES:-<not set>}"
printf 'NVIDIA_VISIBLE_DEVICES: %s\n' "${NVIDIA_VISIBLE_DEVICES:-<not set>}"

if [ -d /usr/local/cuda ]; then
    printf 'OK: /usr/local/cuda exists\n'
else
    missing "/usr/local/cuda"
fi

if [ -x /usr/local/cuda/bin/nvcc ]; then
    printf '/usr/local/cuda/bin/nvcc version:\n'
    /usr/local/cuda/bin/nvcc --version 2>&1 || warning "nvcc returned a non-zero exit status"
elif command -v nvcc >/dev/null 2>&1; then
    printf 'nvcc path: %s\n' "$(command -v nvcc)"
    printf 'nvcc version:\n'
    nvcc --version 2>&1 || warning "nvcc returned a non-zero exit status"
else
    warning "nvcc is not available via /usr/local/cuda/bin/nvcc or the current PATH"
fi

section "GStreamer and DeepStream"
command_version "GStreamer version" gst-inspect-1.0 --version

if command -v deepstream-app >/dev/null 2>&1; then
    printf 'DeepStream app path: %s\n' "$(command -v deepstream-app)"
    printf 'deepstream-app version:\n'
    deepstream-app --version-all 2>&1 || warning "deepstream-app --version-all returned a non-zero exit status"
elif [ -d /opt/nvidia/deepstream ]; then
    warning "DeepStream directory exists at /opt/nvidia/deepstream, but deepstream-app is not on the current PATH"
else
    missing "DeepStream (deepstream-app and /opt/nvidia/deepstream not found)"
fi

printf 'GStreamer DeepStream plugins:\n'
check_gstreamer_plugin nvstreammux
check_gstreamer_plugin nvinfer
check_gstreamer_plugin nvtracker
check_gstreamer_plugin nvvideoconvert

section "Current Python environment"
if command -v python >/dev/null 2>&1; then
    printf 'which python: '
    which python 2>&1 || command -v python
    python --version 2>&1 || warning "python --version returned a non-zero exit status"
else
    missing "python in the current environment"
fi

section "TensorRT"
if command -v ldconfig >/dev/null 2>&1; then
    if ldconfig -p 2>/dev/null | grep -q 'libnvinfer\.so'; then
        printf 'OK: TensorRT library found by ldconfig\n'
        ldconfig -p 2>/dev/null | grep -m 1 'libnvinfer\.so'
    else
        warning "TensorRT library libnvinfer.so was not found by ldconfig"
    fi
else
    warning "ldconfig is unavailable; cannot inspect TensorRT libraries"
fi

if command -v trtexec >/dev/null 2>&1; then
    printf 'trtexec path: %s\n' "$(command -v trtexec)"
    trtexec --version 2>&1 || warning "trtexec --version returned a non-zero exit status"
else
    warning "trtexec is not on the current PATH"
fi

section "PyDS"
if command -v python >/dev/null 2>&1; then
    if python -c 'import pyds; print("OK: pyds import succeeded")' 2>&1; then
        :
    else
        warning "pyds import failed in the current Python environment (not fatal for Stage 0)"
    fi
else
    warning "pyds was not checked because python is unavailable"
fi