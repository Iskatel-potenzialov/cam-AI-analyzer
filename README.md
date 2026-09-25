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