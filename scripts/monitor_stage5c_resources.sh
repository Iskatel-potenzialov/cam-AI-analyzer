#!/usr/bin/env bash
set -eu

command -v nvidia-smi >/dev/null || { printf 'MISSING: nvidia-smi\n' >&2; exit 1; }
command -v ps >/dev/null || { printf 'MISSING: ps\n' >&2; exit 1; }

while :; do
    printf '=== %s ===\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    nvidia-smi --query-gpu=utilization.gpu,memory.used,memory.total,temperature.gpu --format=csv,noheader,nounits
    printf 'stage5b_5cam CPU%% and RSS (KiB):\n'
    ps -C stage5b_5cam -o pid=,%cpu=,rss= 2>/dev/null || true
    sleep 1
done