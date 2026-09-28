#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CAPTURES_DIR="$PROJECT_ROOT/captures"
WINDOW_NAME="events"
CAPTURE_SECONDS=60
FRAME_COUNT=60
TILER_COLUMNS=3
TILER_ROWS=2
ROI_FILM_ALPHA=0.10
# Source 1 coordinates must match kCase2Roi in src/stage5b_5cam.cpp.
SOURCE0_ROI_X1=0.488281250
SOURCE0_ROI_Y1=0.427777778
SOURCE0_ROI_X2=0.933593750
SOURCE0_ROI_Y2=1.000000000
SOURCE1_ROI_X1=0.40401563
SOURCE1_ROI_Y1=0.406944
SOURCE1_ROI_X2=0.90635913
SOURCE1_ROI_Y2=0.997222
CAPTURE_MODE=plain
CAPTURE_PIDS=()
CAPTURE_NAMES=()

usage() {
    cat <<'USAGE'
Usage:
  bash scripts/capture_source0.sh
  bash scripts/capture_source0.sh --roi-film

Captures source0 and source1 from the "events" window for 60 seconds.
The default plain mode preserves the captured image unchanged. --roi-film
adds a semi-transparent cyan film outside each source ROI in capture artifacts only.
USAGE
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

stop_children() {
    local pid
    for pid in "${CAPTURE_PIDS[@]:-}"; do
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -INT "$pid" 2>/dev/null || true
        fi
    done
}

wait_children() {
    local index pid source_name status failed=0
    for index in "${!CAPTURE_PIDS[@]}"; do
        pid="${CAPTURE_PIDS[$index]}"
        source_name="${CAPTURE_NAMES[$index]}"
        [[ -n "$pid" ]] || continue
        set +e
        wait "$pid"
        status=$?
        set -e
        case "$status" in
            0|130) ;;
            *) printf 'ERROR: %s gst-launch child %s failed with exit status %s\n' "$source_name" "$pid" "$status" >&2; failed=1 ;;
        esac
    done
    CAPTURE_PIDS=()
    CAPTURE_NAMES=()
    return "$failed"
}

on_signal() {
    stop_children
    wait_children || true
    trap - EXIT
    exit 130
}
trap on_signal INT TERM
trap 'stop_children; wait_children || true' EXIT

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --roi-film) CAPTURE_MODE=roi-film; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; fail "unknown argument: $1" ;;
    esac
done

for command in xwininfo gst-launch-1.0 gst-inspect-1.0 awk date find sort; do
    command -v "$command" >/dev/null 2>&1 || fail "required command is unavailable: $command"
done
for plugin in ximagesrc videoconvert videorate jpegenc multifilesink nvvideoconvert nvv4l2h264enc h264parse mp4mux; do
    gst-inspect-1.0 "$plugin" >/dev/null 2>&1 || fail "required GStreamer plugin is unavailable: $plugin"
done
if [[ "$CAPTURE_MODE" == roi-film ]]; then
    for plugin in compositor videotestsrc queue; do
        gst-inspect-1.0 "$plugin" >/dev/null 2>&1 || fail "ROI-film requires GStreamer plugin: $plugin"
    done
fi

window_info="$(xwininfo -name "$WINDOW_NAME" 2>&1)" || fail "window '$WINDOW_NAME' was not found"
field() {
    local name="$1"
    awk -F: -v field_name="$name" '$1 ~ "^[[:space:]]*" field_name "[[:space:]]*$" {gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2); print $2; exit}' <<<"$window_info"
}
window_x="$(field 'Absolute upper-left X')"
window_y="$(field 'Absolute upper-left Y')"
window_width="$(field 'Width')"
window_height="$(field 'Height')"
for value_name in window_x window_y window_width window_height; do
    value="${!value_name}"
    [[ "$value" =~ ^-?[0-9]+$ ]] || fail "could not parse $value_name from xwininfo"
done
(( window_width > 0 && window_height > 0 )) || fail "window dimensions must be positive"

# Adjacent source rectangles share exact global tiler boundaries. ximagesrc end
# coordinates are exclusive, so x2/y2 can be passed to endx/endy directly.
col0=$(( 0 * window_width / TILER_COLUMNS ))
col1=$(( 1 * window_width / TILER_COLUMNS ))
col2=$(( 2 * window_width / TILER_COLUMNS ))
row0=$(( 0 * window_height / TILER_ROWS ))
row1=$(( 1 * window_height / TILER_ROWS ))
source0_x=$(( window_x + col0 ))
source0_y=$(( window_y + row0 ))
source0_end_x=$(( window_x + col1 ))
source0_end_y=$(( window_y + row1 ))
source0_width=$(( source0_end_x - source0_x ))
source0_height=$(( source0_end_y - source0_y ))
source1_x=$(( window_x + col1 ))
source1_y=$(( window_y + row0 ))
source1_end_x=$(( window_x + col2 ))
source1_end_y=$(( window_y + row1 ))
source1_width=$(( source1_end_x - source1_x ))
source1_height=$(( source1_end_y - source1_y ))
(( source0_width > 0 && source0_height > 0 && source1_width > 0 && source1_height > 0 )) || fail "source capture dimensions must be positive"

calculate_roi_pixels() {
    local tile_width="$1" tile_height="$2" x1="$3" y1="$4" x2="$5" y2="$6"
    awk -v width="$tile_width" -v height="$tile_height" -v x1="$x1" -v y1="$y1" -v x2="$x2" -v y2="$y2" '
        function rounded(value) { return int(value + 0.5) }
        function clamp(value, low, high) { return value < low ? low : (value > high ? high : value) }
        BEGIN {
            left=clamp(rounded(x1 * width), 0, width)
            top=clamp(rounded(y1 * height), 0, height)
            right=clamp(rounded(x2 * width), left, width)
            bottom=clamp(rounded(y2 * height), top, height)
            printf "%d %d %d %d\n", left, top, right - left, bottom - top
        }'
}

read -r source0_roi_x source0_roi_y source0_roi_width source0_roi_height < <(
    calculate_roi_pixels "$source0_width" "$source0_height" "$SOURCE0_ROI_X1" "$SOURCE0_ROI_Y1" "$SOURCE0_ROI_X2" "$SOURCE0_ROI_Y2"
)
read -r source1_roi_x source1_roi_y source1_roi_width source1_roi_height < <(
    calculate_roi_pixels "$source1_width" "$source1_height" "$SOURCE1_ROI_X1" "$SOURCE1_ROI_Y1" "$SOURCE1_ROI_X2" "$SOURCE1_ROI_Y2"
)

run_timestamp="$(date +%Y-%m-%d_%H-%M-%S)"
run_dir="$CAPTURES_DIR/${run_timestamp}_${CAPTURE_MODE}"
[[ ! -e "$run_dir" ]] || fail "capture run directory already exists: $run_dir"
source0_dir="$run_dir/source0"
source1_dir="$run_dir/source1"
mkdir -p "$source0_dir/frames" "$source1_dir/frames"

printf 'CAPTURE_MODE mode=%s\n' "$CAPTURE_MODE"
printf 'CAPTURE_WINDOW x=%s y=%s width=%s height=%s\n' "$window_x" "$window_y" "$window_width" "$window_height"
printf 'CAPTURE_SOURCE0 x=%s y=%s width=%s height=%s\n' "$source0_x" "$source0_y" "$source0_width" "$source0_height"
printf 'CAPTURE_SOURCE1 x=%s y=%s width=%s height=%s\n' "$source1_x" "$source1_y" "$source1_width" "$source1_height"
printf 'CAPTURE_ROI_SOURCE0 x=%s y=%s width=%s height=%s\n' "$source0_roi_x" "$source0_roi_y" "$source0_roi_width" "$source0_roi_height"
printf 'CAPTURE_ROI_SOURCE1 x=%s y=%s width=%s height=%s\n' "$source1_roi_x" "$source1_roi_y" "$source1_roi_width" "$source1_roi_height"
printf 'CAPTURE_RUN_DIR path=%s\n' "$run_dir"

start_plain_capture() {
    local source_name="$1" start_x="$2" start_y="$3" end_x="$4" end_y="$5" source_dir="$6"
    gst-launch-1.0 -e \
        ximagesrc use-damage=0 show-pointer=false startx="$start_x" starty="$start_y" endx="$end_x" endy="$end_y" ! \
        video/x-raw,framerate=30/1 ! tee name="${source_name}_tee" \
        "${source_name}_tee". ! queue ! videoconvert ! nvvideoconvert ! video/x-raw\(memory:NVMM\),format=NV12 ! \
        nvv4l2h264enc ! h264parse ! mp4mux ! filesink location="$source_dir/${source_name}_60s.mp4" \
        "${source_name}_tee". ! queue ! videorate ! video/x-raw,framerate=1/1 ! videoconvert ! jpegenc ! \
        multifilesink location="$source_dir/frames/frame_%03d.jpg" index=1 &
    CAPTURE_PIDS+=("$!")
    CAPTURE_NAMES+=("$source_name")
}

start_roi_film_capture() {
    local source_name="$1" start_x="$2" start_y="$3" end_x="$4" end_y="$5" source_dir="$6"
    local tile_width="$7" tile_height="$8" roi_x="$9" roi_y="${10}" roi_width="${11}" roi_height="${12}"
    local roi_right=$(( roi_x + roi_width )) roi_bottom=$(( roi_y + roi_height ))
    local mixer_name="${source_name}_mix" pad_index=1
    local -a mixer_args=(compositor name="$mixer_name" background=black)
    local -a film_args=()

    add_film_region() {
        local region_x="$1" region_y="$2" region_width="$3" region_height="$4" pad_name
        (( region_width > 0 && region_height > 0 )) || return 0
        pad_name="sink_${pad_index}"
        mixer_args+=("${pad_name}::xpos=${region_x}" "${pad_name}::ypos=${region_y}" "${pad_name}::alpha=${ROI_FILM_ALPHA}")
        film_args+=(videotestsrc is-live=true pattern=solid-color foreground-color=0xff00ffff ! "video/x-raw,format=I420,width=${region_width},height=${region_height},framerate=30/1" ! queue ! "${mixer_name}.${pad_name}")
        (( pad_index += 1 ))
    }

    add_film_region 0 0 "$tile_width" "$roi_y"
    add_film_region 0 "$roi_bottom" "$tile_width" $(( tile_height - roi_bottom ))
    add_film_region 0 "$roi_y" "$roi_x" "$roi_height"
    add_film_region "$roi_right" "$roi_y" $(( tile_width - roi_right )) "$roi_height"

    gst-launch-1.0 -e \
        "${mixer_args[@]}" ! tee name="${source_name}_tee" \
        ximagesrc use-damage=0 show-pointer=false startx="$start_x" starty="$start_y" endx="$end_x" endy="$end_y" ! \
        video/x-raw,framerate=30/1 ! videoconvert ! video/x-raw,format=I420,framerate=30/1 ! "${mixer_name}.sink_0" \
        "${film_args[@]}" \
        "${source_name}_tee". ! queue ! videoconvert ! nvvideoconvert ! video/x-raw\(memory:NVMM\),format=NV12 ! \
        nvv4l2h264enc ! h264parse ! mp4mux ! filesink location="$source_dir/${source_name}_60s.mp4" \
        "${source_name}_tee". ! queue ! videorate ! video/x-raw,framerate=1/1 ! videoconvert ! jpegenc ! \
        multifilesink location="$source_dir/frames/frame_%03d.jpg" index=1 &
    CAPTURE_PIDS+=("$!")
    CAPTURE_NAMES+=("$source_name")
}

start_capture() {
    local source_name="$1" start_x="$2" start_y="$3" end_x="$4" end_y="$5" source_dir="$6" tile_width="$7" tile_height="$8" roi_x="$9" roi_y="${10}" roi_width="${11}" roi_height="${12}"
    if [[ "$CAPTURE_MODE" == roi-film ]]; then
        start_roi_film_capture "$source_name" "$start_x" "$start_y" "$end_x" "$end_y" "$source_dir" "$tile_width" "$tile_height" "$roi_x" "$roi_y" "$roi_width" "$roi_height"
    else
        start_plain_capture "$source_name" "$start_x" "$start_y" "$end_x" "$end_y" "$source_dir"
    fi
}

start_capture source0 "$source0_x" "$source0_y" "$source0_end_x" "$source0_end_y" "$source0_dir" "$source0_width" "$source0_height" "$source0_roi_x" "$source0_roi_y" "$source0_roi_width" "$source0_roi_height"
start_capture source1 "$source1_x" "$source1_y" "$source1_end_x" "$source1_end_y" "$source1_dir" "$source1_width" "$source1_height" "$source1_roi_x" "$source1_roi_y" "$source1_roi_width" "$source1_roi_height"
sleep "$CAPTURE_SECONDS"
stop_children
wait_children || fail "one or more GStreamer capture processes failed"

validate_source() {
    local source_name="$1"
    local source_dir="$2"
    local mp4_path="$source_dir/${source_name}_60s.mp4"
    local frame expected_frame
    local -a frames=()
    [[ -s "$mp4_path" ]] || { printf 'ERROR: %s MP4 was not created or is empty: %s\n' "$source_name" "$mp4_path" >&2; return 1; }
    mapfile -t frames < <(find "$source_dir/frames" -maxdepth 1 -type f -name 'frame_*.jpg' -printf '%f\n' | LC_ALL=C sort)
    if (( ${#frames[@]} > FRAME_COUNT )); then
        for frame in "${frames[@]:FRAME_COUNT}"; do
            rm -f -- "$source_dir/frames/$frame"
        done
        frames=("${frames[@]:0:FRAME_COUNT}")
    fi
    if (( ${#frames[@]} < FRAME_COUNT )); then
        printf 'ERROR: %s expected %s JPEG files, found %s\n' "$source_name" "$FRAME_COUNT" "${#frames[@]}" >&2
        return 1
    fi
    for (( index=1; index<=FRAME_COUNT; ++index )); do
        expected_frame=$(printf 'frame_%03d.jpg' "$index")
        [[ -s "$source_dir/frames/$expected_frame" ]] || { printf 'ERROR: %s missing expected JPEG: %s\n' "$source_name" "$expected_frame" >&2; return 1; }
    done
    printf '%s' "${#frames[@]}"
}

validation_failed=0
source0_jpeg_count="$(validate_source source0 "$source0_dir")" || validation_failed=1
source1_jpeg_count="$(validate_source source1 "$source1_dir")" || validation_failed=1
(( validation_failed == 0 )) || fail "capture validation failed; artifacts were retained in $run_dir"
printf 'CAPTURE_COMPLETE run_dir=%s mode=%s source0_jpeg_count=%s source1_jpeg_count=%s\n' "$run_dir" "$CAPTURE_MODE" "$source0_jpeg_count" "$source1_jpeg_count"
