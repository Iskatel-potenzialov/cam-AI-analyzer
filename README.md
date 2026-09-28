# DeepStream Car Tracking

Локальный проект многокамерной видеоаналитики на NVIDIA DeepStream.

Цель V1 — принимать несколько HLS/RTSP потоков, аппаратно декодировать их на NVIDIA GPU, детектировать автомобили через YOLOv8/TensorRT и отслеживать их внутри каждой камеры.

## Архитектура

```text
HLS / RTSP
    ↓
NVDEC / GStreamer
    ↓
nvstreammux
    ↓
nvinfer + YOLOv8 TensorRT
    ↓
nvtracker
    ↓
event logic
    ↓
metadata / logs / optional JPEG
```

V1 использует **local per-camera tracking**. Cross-camera ReID/MTMC в первую версию не входит.

## Целевая среда

Ubuntu runtime:

- Ubuntu 22.04.5 LTS
- NVIDIA RTX 3060 12 GB
- CUDA 12.2
- cuDNN 8.9
- TensorRT 8.6
- DeepStream SDK 7.0
- GStreamer 1.20.x

Базовая DeepStream установка уже проверена встроенным NVIDIA sample pipeline.

## Пути

Проект на Ubuntu:

```text
/home/evgeny/CV/deepstream-car-tracking
```

Python venv:

```text
/home/evgeny/CV/CV-venv
```

На Windows проект подключён через SSHFS как:

```text
Z:\
```

## Как ведётся разработка

Codex работает локально в VS Code на Windows и редактирует файлы проекта через `Z:`.

Реальные проверки DeepStream, CUDA, TensorRT, GPU и видеопотоков выполняются вручную на Ubuntu.

Работа идёт небольшими этапами:

1. сформулировать одну задачу;
2. Codex изучает существующий код;
3. делает минимальные изменения;
4. выполняет доступные локальные проверки;
5. сообщает команды, которые нужно проверить на Ubuntu;
6. пользователь запускает их на Ubuntu;
7. только после успешной проверки начинается следующий этап.

Перед работой Codex должен прочитать:

- `AGENTS.md`
- `SPEC.md`
- `README.md`

## Основные правила V1

- полный кадр, без старого split/crop;
- аппаратный decode;
- GPU resize;
- batching через `nvstreammux`;
- YOLOv8 COCO;
- первый целевой класс — `car`;
- inference через TensorRT + `nvinfer`;
- tracking через `nvtracker`;
- без cross-camera identity;
- конфиги вместо хардкода;
- секреты камер не хранить в Git;
- не усложнять архитектуру без необходимости.

## Старый prototype

Старый `scaner_5cam_nebo.py` можно использовать только как источник сведений о HLS-потоках, headers и поведении камер.

Его архитектура не переносится в новую систему.

## Документы

- `SPEC.md` — техническое задание и архитектурные ограничения.
- `AGENTS.md` — обязательные правила работы Codex.
- `README.md` — краткое описание проекта и рабочий workflow.

## Stage 1: one local DeepStream source

The Stage 1 check is headless and uses the NVIDIA DeepStream H.264 sample by default:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
bash scripts/run_stage1_local_file.sh
```

To use another local H.264 elementary stream, pass its path as the first argument. Success means the pipeline reaches EOS and the command exits with status `0`.

## Stage 2A: sample nvinfer detector

Stage 2A keeps one local source and adds the installed DeepStream primary-detector configuration, without copying its model files:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
bash scripts/run_stage2a_nvinfer.sh
```

The pipeline is headless. Success means `nvinfer` loads the installed sample model, the pipeline reaches EOS, and the command exits with status `0`.

## Stage 2B1: YOLOv8n ONNX export

With `/home/evgeny/CV/CV-venv` active, export the standard pretrained YOLOv8n COCO model and validate its ONNX artifact:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
python scripts/export_yolov8n_onnx.py
```

Weights are stored at `models/yolov8n/weights/yolov8n.pt`; the checked ONNX artifact is stored at `models/yolov8n/artifacts/yolov8n.onnx`. These binary artifacts are excluded from Git.

## Stage 2B2: YOLOv8n TensorRT FP16 engine

Build a static batch-1 FP16 TensorRT engine from the checked ONNX artifact:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
bash scripts/build_yolov8n_fp16_engine.sh
```

The script uses `/usr/src/tensorrt/bin/trtexec` on GPU 0 and writes `models/yolov8n/artifacts/yolov8n_fp16.engine`. The engine is excluded from Git.

## Stage 2B3A: parser-layout YOLOv8n ONNX

Create the parser-compatible output layout and then build its static FP16 TensorRT engine:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
python scripts/transpose_yolov8n_onnx.py
bash scripts/build_yolov8n_transposed_fp16_engine.sh
```

The source `yolov8n.onnx` and baseline engine remain unchanged. The new artifacts are `models/yolov8n/artifacts/yolov8n_transposed.onnx` and `models/yolov8n/artifacts/yolov8n_fp16_transposed.engine`.

## Stage 2B3B: CPU YOLOv8 parser library

Build the project-local CPU parser library against DeepStream 7.0:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
bash scripts/build_yolov8_parser.sh
nm -D parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so | grep 'NvDsInferParseCustomYoloV8$'
```

The expected library is `parsers/yolov8/libnvdsinfer_custom_impl_Yolo.so`. This stage builds and inspects the parser ABI only; it does not integrate the parser with `nvinfer`.

## Stage 2B3C: YOLOv8n DeepStream smoke test

Run the headless one-source pipeline with the existing transposed FP16 engine and CPU parser:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
bash scripts/run_stage2b3c_yolov8_deepstream.sh
```

The launcher renders absolute Ubuntu paths into a temporary nvinfer config, verifies `NvDsInferParseCustomYoloV8`, and runs `filesrc -> h264parse -> nvv4l2decoder -> nvstreammux -> nvinfer -> fakesink`. It does not rebuild the engine or inspect metadata.

## Stage 2B3D: nvinfer detector metadata probe

Build the minimal native probe and run the same headless single-source YOLOv8n pipeline:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src
bash scripts/run_stage2b3d_metadata.sh
```

The probe is attached to `nvinfer`'s source pad. It reads `NvDsBatchMeta`, `NvDsFrameMeta`, and `NvDsObjectMeta`, then logs the frame number, object count, class ID, label, and confidence for at most the first 20 frames that contain detections. The pipeline continues silently to EOS. It reuses the existing transposed FP16 engine and parser and does not rebuild either artifact.
## Stage 3: local NvDCF tracking

Build the existing native utility and run one local source through the project YOLOv8n detector and the installed DeepStream NvDCF tracker:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src
bash scripts/run_stage3_nvdcf_tracking.sh
```

The launcher uses only a child-process `LD_LIBRARY_PATH` for DeepStream runtime libraries. It passes the installed `libnvds_nvmultiobjecttracker.so` and the NVIDIA `config_tracker_NvDCF_perf.yml` baseline to `nvtracker` with `tracker-width=960`, `tracker-height=544`, and `gpu-id=0`. The probe is on `nvtracker`'s source pad and logs `object_id` with detection metadata for at most 20 frames containing detections.
## Stage 4: one live RTSP source

Build the RTSP utility and run the same detector/tracker chain with one camera. Pass the RTSP URI as an argument; it is not stored in C++ source or project configuration:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src
bash scripts/run_stage4_rtsp_tracking.sh 'rtsp://192.168.0.57/id=0'
```

The utility links only a dynamic `rtspsrc` pad whose caps identify an H.264 video RTP stream; audio pads are ignored. It uses TCP transport, `latency=200`, `nvstreammux live-source=1`, the existing YOLOv8n engine/parser, and the Stage 3 NvDCF baseline. The metadata probe is on `nvtracker`'s source pad. Stop the live pipeline with Ctrl+C; it transitions to `GST_STATE_NULL` and exits successfully unless GStreamer reports an error.
## Stage 4B: live RTSP display with tracking overlay

Build the separate display utility and pass one RTSP URI to its launcher:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src
bash scripts/run_stage4b_rtsp_display.sh 'rtsp://192.168.0.57/id=0'
```

The display tail is `nvtracker -> nvvideoconvert -> capsfilter -> nvdsosd -> nveglglessink`. The capsfilter is exactly `video/x-raw(memory:NVMM),format=RGBA`, so video remains in NVMM. The probe stays on `nvtracker`'s source pad: it preserves the tracker rectangle and writes each object's `text_params.display_text` as `<label> <confidence> ID:<object_id>`. Console metadata remains limited to the first 20 frames with detections, while display text continues to update. Stop the local Ubuntu window with Ctrl+C.
## Stage 5A.0: static batch-2 YOLOv8n engine

Keep the batch-1 artifacts used by Stage 4B unchanged and create separate static batch-2 ONNX and FP16 TensorRT artifacts:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
python scripts/export_yolov8n_onnx.py --batch 2
python scripts/transpose_yolov8n_onnx.py --batch 2
bash scripts/build_yolov8n_transposed_b2_fp16_engine.sh
```

The new artifacts are `models/yolov8n/artifacts/yolov8n_b2.onnx`, `models/yolov8n/artifacts/yolov8n_b2_transposed.onnx`, and `models/yolov8n/artifacts/yolov8n_fp16_transposed_b2.engine`. The scripts require and print static shapes `[2,3,640,640]`, `[2,84,8400]`, and `[2,8400,84]` respectively. They do not modify the parser or the batch-1 nvinfer config.
## Stage 5A.1: one HLS source decode validation

Build the separate HLS utility, then pass the HLS URL only as a runtime argument; it is not stored or printed:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src stage5a1_hls_decode
bash scripts/run_stage5a1_hls_decode.sh '<HLS_URL>' 60
```

The one-source validation configures the required User-Agent and Referer through `souphttpsrc`, dynamically links only `video/mpegts` from `hlsdemux` and `video/x-h264` from `tsdemux`, and requires byte-stream H.264 access units before `nvv4l2decoder`. It contains no inference, mux, tracker, display, recording, or reconnect logic. A probe logs the first decoded buffer, every 300th buffer, and the final total. Omit `60` to run until Ctrl+C.
## Stage 5A: two-source batch-2 DeepStream display

Build the separate utility and pass the RTSP and HLS URLs only at runtime:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src stage5a_multicam
bash scripts/run_stage5a_multicam.sh 'rtsp://192.168.0.57/id=0' '<HLS_URL>'
```

The single pipeline connects RTSP source ID 0 and HLS source ID 1 to one live `nvstreammux` with `batch-size=2`, then runs the batch-2 YOLOv8n engine through one `nvinfer` and one NvDCF tracker. The display tail is `nvmultistreamtiler` (1×2, 1280×360) → `nvvideoconvert` → `video/x-raw(memory:NVMM),format=RGBA` → `nvdsosd` → `nveglglessink`. The probe on `nvtracker:src` logs at most 20 detection frames per source and sets each overlay to `cam:<source_id> <label> <confidence> ID:<object_id>`. URLs are not printed; URL occurrences in fatal GStreamer messages are redacted.
## Stage 5B: five HLS sources, batch 5

Create the static batch-5 artifacts and build the five-source utility:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
python scripts/export_yolov8n_onnx.py --batch 5
python scripts/transpose_yolov8n_onnx.py --batch 5
bash scripts/build_yolov8n_transposed_b5_fp16_engine.sh
make -C src stage5b_5cam
```

For local testing, create `config/local_hls_sources.txt` from `config/local_hls_sources.example.txt` and place exactly five full HLS URLs there, one per line. The local file is ignored by Git and must never be committed. Run:

```bash
bash scripts/run_stage5b_5cam.sh
```

The launcher validates the local file without printing its URLs. Passing five URLs explicitly remains supported for one-off runs. For a display-path control test, run `bash scripts/run_stage5b_5cam.sh --no-display`; only the terminal `nveglglessink` is replaced with `fakesink` after `nvdsosd`.

The one pipeline uses five HLS branches. Each branch uses `nvv4l2decoder -> identity sync=true -> nvstreammux`; `identity` releases decoded frames according to timestamps before the mux, preventing bursty HLS-segment delivery from reaching the batcher. The pipeline uses one `nvstreammux` with `batch-size=5`, one batch-5 `nvinfer`, one NvDCF tracker, and a 2×3 1920×720 tiler with 640×360 cells. It has been manually validated with five HLS cameras: NVIDIA decode, batch-5 TensorRT inference, NvDCF tracking, tiled display, clean SIGINT shutdown, and exit status `0`. `--no-display` remains available for headless runs.

Known runtime note: on the tested GStreamer 1.20.3 / libsoup 2.74.2 stack, periodic `gst_query_set_context` and `gst_element_set_context` critical assertions were observed around `gst.soup.session` `HAVE_CONTEXT` activity in the internal `souphttpsrc` HLS stack. The pipeline remained operational and exited with status `0`. No project-level workaround is applied; this is an observed correlation, not a confirmed upstream GStreamer bug.
## Stage 5C: performance baseline

Stage 5C measures the existing Stage 5B baseline without changing its CV pipeline. Build the utility, then run the opt-in benchmark with display:

```bash
cd /home/evgeny/CV/deepstream-car-tracking
make -C src stage5b_5cam
bash scripts/run_stage5b_5cam.sh --benchmark
```

For a headless comparison, use:

```bash
bash scripts/run_stage5b_5cam.sh --benchmark --no-display
```

The flags can be supplied in either order. Every five seconds the benchmark reports each source's frame rate and final tiled pipeline output rate. Source FPS is counted from `NvDsFrameMeta` after tracking; pipeline output is explicitly reported as `batches/s`, because `nvstreammux` uses batch size 5. On shutdown it prints total runtime, per-source frames and average FPS, output batches, and average batches/s. No benchmark result is recorded here until a real Ubuntu run.

In a separate terminal, monitor the active process with:

```bash
bash scripts/monitor_stage5c_resources.sh
```

The read-only monitor samples GPU utilization, VRAM used/total, temperature, and `stage5b_5cam` CPU percent/RSS once per second through `nvidia-smi` and `ps`.
## Stage 6: person line crossing

Stage 6 adds business logic only for `source_id=0`: COCO `person` (`class_id=0`) tracks from NvDCF are counted when their bottom-center bounding-box point crosses the configured virtual line. The geometry is normalized: the ROI is the left half of the top third of the full mux frame, and the line is `(1.0, 0.5) -> (0.0, 1.0)` relative to that ROI. The mapping is explicit in `kCamera0Analytics`: side A → side B is `IN`; the reverse is `OUT`.

The implementation uses a signed orientation test, a normalized epsilon/hysteresis zone, and per-`object_id` last-seen cleanup. ROI boundary, line, and `IN`/`OUT` counters are attached as DeepStream display metadata on source 0 before tiling. Sources 1–4 retain their existing behavior. The ROI affects analytics only: there is no physical pre-inference crop, so YOLO continues to process each complete frame. Stage 5C `--benchmark` and `--no-display` remain available.
For temporary Stage 6 validation, run `bash scripts/run_stage5b_5cam.sh --line-debug`. This opt-in diagnostic mode logs only significant `person` track transitions for source 0 relative to its ROI and virtual line; normal runs remain unchanged.
## Stage 6A: pre-inference ROI for source 0

Stage 6A uses GPU `nvdspreprocess` before `nvinfer`: `nvstreammux` outputs 2560×1440 and all five sources use full-frame preprocessing into the static 5×3×640×640 tensor. Stage 7 keeps its source-0 ROI exclusively for vehicle analytics; it is not a pre-inference crop. The display remains full frame. `nvinfer` consumes the preprocessed tensor metadata before NvDCF and analytics. Detection quality is not claimed until a manual Ubuntu runtime comparison.
For temporary source-0 person detection and NvDCF tracking diagnostics, use `bash scripts/run_stage5b_5cam.sh --quality-debug`. The opt-in mode reports significant track lifetime, confidence, bbox-jitter, and possible ID-switch signals; it does not alter detector or tracker settings.
## Stage 7: vehicle multi-line counting

`bash scripts/run_stage5b_5cam.sh --vehicle-line-debug` enables source-0 vehicle analytics without changing the five-source DeepStream pipeline. It counts only COCO `car` (2), `motorcycle` (3), and `bus` (5) using the bottom-center tracker point inside the normalized ROI `[0.488281250, 0.427777778]–[0.933593750, 1.0]`. Red counts only confirmed bottom-to-top movement (`current_y < previous_y`); green counts both left-to-right and right-to-left movement; the blue line counts only a confirmed crossing whose current x-position is smaller than the previous x-position. Per-line, per-object state prevents a tracked object from being counted repeatedly because of bbox jitter. Source 0 overlays the cyan ROI, red/green/blue lines, and counters; display remains full frame. `--vehicle-quality-debug` is an independent, combinable source-0 diagnostic mode for car/motorcycle/bus detector and NvDCF tracking quality. It reports bounded lifecycle, confidence, bbox-jitter, observation-gap, and possible-ID-switch signals without changing model, threshold, tracker, or crossing decisions. Before NvDCF, source 0 retains only car, motorcycle, and bus metadata; sources 1–4 are unchanged. Stage 7D.1 uses one C++ Stage 7 geometry definition for analytics and OSD, while the launcher validates that the GPU pre-inference crop matches it (`1250;616;1140;824` in the 2560×1440 mux frame) before resizing it to the existing 640×640 tensor; sources 1–4 remain full-frame. This is a quality experiment; no improvement is claimed before a manual comparison.

--vehicle-line-zone-debug is an opt-in visual diagnostic for source 0. RED, GREEN, and BLUE use bounded pixel-space segments: a point must project to t in [0, 1] and be within the shared +/-15 px capture half-width. The overlay draws each finite capture rectangle, endpoints, and the original segments. Crossing decisions additionally require the bottom-center motion segment to intersect the finite counting segment. It does not change detector, tracker, or direction-counter semantics.
## Post-V1 Stage 8A backend bootstrap

`backend/` is an independent FastAPI/PostgreSQL bootstrap for a future CV-event storage and delivery layer. It uses the separate Ubuntu environment `/home/evgeny/CV/backend-venv`; it does not use or modify `CV-venv` or the DeepStream pipeline. See `backend/README.md` for the manual setup workflow.

## Stage 8D: asynchronous backend events

When the Stage 7 source-0 vehicle logic accepts a line crossing after its existing bounded-segment, direction, two-frame confirmation, and dedup checks, it enqueues one `line_crossing` event for the local FastAPI API. Delivery runs in a separate libcurl worker thread; the video pipeline never performs HTTP directly. The queue is bounded to 100 events. HTTP `200` and `201` are success; network errors and `5xx` receive at most two retries using the same `event_id`; `4xx` is not retried. The default endpoint is `http://127.0.0.1:8000/api/v1/events`; override it with `EVENT_API_URL=...` or `--event-api-url URL`. No endpoint URL is printed by the launcher. Every `stage5b_5cam` process generates one UUID at startup (`RUN_START run_id=...`); all events from that process store it as `attributes.run_id`. A restarted process receives a new run ID. Use the ordinary launch command `bash scripts/run_stage5b_5cam.sh --vehicle-line-debug`; its default `EVENT_API_URL` is `http://127.0.0.1:8000/api/v1/events`. Override it only when needed with `EVENT_API_URL=...` or `--event-api-url URL`. Shutdown prints `RUN_SUMMARY` with line counters and enqueued/sent/failed/queue-full delivery totals. Compare one run with `SELECT rule_id, direction, count(*) FROM events WHERE attributes ->> 'run_id' = '<run-id>' GROUP BY rule_id, direction;`.
## Case 2C.2: source1 live state

Source 1 continues to send `red_zone_entry` as a persistent event through `EVENT_API_URL`. Separately, it posts its current confirmed `RED` / `YELLOW` / `GREEN` person snapshot to `LIVE_STATE_API_URL` (default `http://127.0.0.1:8000/api/v1/live-state`) at most once per second. The backend stores this live snapshot only in RAM; it is not event history and is lost when the backend restarts. Override only this endpoint with `LIVE_STATE_API_URL=... bash scripts/run_stage5b_5cam.sh`.

## Post-V1: Case 2 live JPEG snapshot

Case 2 exposes the latest source1 frame through a separate non-blocking branch after `nvtracker`: `tee -> bounded/leaky queue -> nvstreamdemux src_1 -> RGBA/NVMM -> nvdsosd -> I420/NVMM -> nvjpegenc -> appsink`. It reuses existing tracker metadata and Case 2 OSD; it does not run inference, TensorRT, NvDCF, or zone classification again. At most one JPEG per second is encoded and atomically replaces `runtime/source1_snapshot.jpg`; this runtime artifact is ignored by Git. FastAPI serves it at `GET /api/v1/cameras/source1/snapshot.jpg` (`404` until the first frame), and the Case 2 page refreshes its `<img>` once per second with a cache-busting query parameter. The ordinary display branch continues independently after the tracker tee.