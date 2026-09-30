# Инспекция домино на Jetson Nano

Система визуального контроля поверхности домино на NVIDIA Jetson Nano.

Jetson локально получает изображение с CSI-камеры, через YOLOv5 + TensorRT находит домино, получает high-resolution crop и затем отдельным процессом запускает DINO ViT-S/16 для поиска аномалий поверхности. Результат `GOOD / DEFECT` вместе с изображениями и anomaly scores отправляется на Ubuntu, сохраняется в PostgreSQL и отображается во frontend.

Case 3 работает отдельно от основного DeepStream-конвейера Case 1 / Case 2. Raw video на Ubuntu не передаётся — inference выполняется непосредственно на edge-устройстве.

---

## Что умеет система

| Этап | Что происходит |
|---|---|
| Поиск объекта | YOLOv5 находит домино в кадре `1280×720` |
| Стабилизация | система ждёт несколько стабильных детекций подряд |
| High-resolution capture | камера переключается на `3264×2464` |
| Точная локализация | YOLOv5 повторно определяет bbox на high-resolution кадре |
| Выбор кадра | из нескольких кадров выбирается наиболее резкий |
| Crop | формируется изображение только с домино |
| Анализ поверхности | DINO ViT-S/16 сравнивает поверхность с GOOD-эталоном |
| Результат | формируется `GOOD` или `DEFECT` |
| Отправка | JSON и изображения передаются в Inspection API |

---

## Архитектура

```text
CSI Camera
    │
    ▼
1280×720
YOLOv5 + TensorRT
поиск домино
    │
    ▼
STABILIZE
проверка стабильности объекта
    │
    ▼
3264×2464
high-resolution capture
    │
    ▼
YOLOv5 + TensorRT
точный bbox на большом кадре
    │
    ▼
выбор самого резкого кадра
    │
    ▼
crop домино
    │
    ▼
YOLO process завершается
    │
    ▼
DINO ViT-S/16
anomaly detection поверхности
    │
    ▼
GOOD / DEFECT
    │
    ▼
inspection_result.json + изображения
    │
    ▼
HTTP POST
    │
    ▼
Inspection API / Ubuntu
```

YOLO и DINO специально работают в разных процессах, чтобы одновременно не занимать память Jetson Nano.

---

## Как проходит инспекция

Сначала камера работает в режиме `1280×720`. YOLOv5 находит объект класса `dm`, а система ждёт несколько стабильных bbox подряд.

После стабилизации камера переключается в режим `3264×2464`. YOLOv5 повторно находит домино уже на high-resolution кадре. Из нескольких кадров выбирается наиболее резкий, после чего сохраняется `03_crop.jpg` — основной вход для DINO.

Схема первого этапа:

```text
CSI Camera
    ↓
1280×720
    ↓
YOLOv5
    ↓
STABILIZE
    ↓
3264×2464
    ↓
YOLOv5
    ↓
best frame
    ↓
crop
```

---

## DINO anomaly detection

После завершения YOLO-процесса запускается отдельный DINO worker.

Используется:

```text
DINO ViT-S/16
```

Crop приводится к `224×224`. При patch size `16×16` получается сетка:

```text
14 × 14 = 196 patches
```

Для каждого patch формируется embedding размерности `384`.

Каждый patch тестового изображения сравнивается со всеми patch embeddings GOOD-эталона:

```text
test patch
    ↓
все GOOD patches
    ↓
best cosine similarity
    ↓
anomaly score = 1 - best cosine similarity
```

Текущий экспериментальный threshold:

```text
0.40
```

Если значимых anomaly regions нет — результат `GOOD`. Если они есть — `DEFECT`.

---

## Memory-safe архитектура

Jetson Nano имеет ограниченный объём памяти. YOLO использует TensorRT, а DINO — PyTorch, поэтому текущий pipeline разделён:

```text
inspection_pipeline_memory_safe.py
        ↓
YOLO child process
        ↓
process полностью завершается
        ↓
DINO child process
        ↓
process полностью завершается
```

Главный controller сам не загружает TensorRT и PyTorch модели.

---

## Основные файлы

```text
src/
├── detector_legacy.py
├── capture_experiment.py
├── inspection_pipeline_memory_safe.py
├── inspection_analyze_sample.py
├── wait_remove_worker.py
├── inspection_result.py
└── inspection_sender.py
```

`detector_legacy.py` — TensorRT wrapper для YOLOv5.

`capture_experiment.py` — low-res SEARCH, STABILIZE, high-resolution capture, повторная детекция и выбор crop.

`inspection_pipeline_memory_safe.py` — главный controller, который последовательно запускает YOLO и DINO в разных процессах.

`inspection_analyze_sample.py` — DINO-анализ crop и формирование `GOOD / DEFECT`.

`inspection_result.py` — формирование итогового `inspection_result.json`.

`inspection_sender.py` — отправка результата и изображений на Ubuntu.

`wait_remove_worker.py` — ожидание удаления объекта перед следующей инспекцией.

---

## Результат инспекции

Для одной проверки создаются:

```text
00_search_full.jpg
00_search_bbox.jpg
01_full.jpg
02_full_bbox.jpg
03_crop.jpg
07_anomaly_heatmap.jpg
08_anomaly_overlay.jpg
09_top_anomalies_filtered.jpg
metadata.json
inspection_result.json
```

`inspection_result.json` содержит результат `GOOD / DEFECT`, YOLO confidence и bbox, DINO anomaly scores, полную карту `14×14`, anomaly regions и показатели качества изображения.

---

## Отправка на Ubuntu

После анализа Jetson отправляет результат через:

```text
POST /api/v1/inspections
```

Текущая схема:

```text
Jetson Nano
    ↓
Inspection API :8010
    ↓
PostgreSQL + image storage
    ↓
React frontend
```

---

## Запуск

Одна полная инспекция:

```bash
cd /home/nvideo/domino_inspection
python3 src/inspection_pipeline_memory_safe.py --once
```

Без отправки на Ubuntu:

```bash
python3 src/inspection_pipeline_memory_safe.py --once --no-send
```

В непрерывном режиме после инспекции используется `WAIT_REMOVE`, после чего система возвращается к SEARCH.

---

## Модели

Бинарные model files в Git не включаются.

На Jetson ожидаются:

```text
models/
├── best_domino_280424.engine
└── libmyplugins.so
```

Текущий TensorRT engine — custom YOLOv5 с классом:

```text
dm
```

DINO используется как `dino_vits16` из Facebook Research DINO.

GOOD reference ожидается по пути:

```text
results/experiment_01/good_reference/03_crop.jpg
```

---

## Целевая среда

- NVIDIA Jetson Nano
- Ubuntu 18.04.6 LTS
- L4T R32.7.4
- Python 3.6.9
- CUDA 10.2.460
- TensorRT 8.2.1.8
- PyTorch 1.10.0
- torchvision 0.9.0
- OpenCV 4.1.1
- NumPy 1.19.0
- PyCUDA 2020.1
- CSI camera IMX219

Рабочее окружение старое и чувствительное к обновлениям. Не рекомендуется выполнять `apt upgrade`, менять JetPack, CUDA, TensorRT, системный Python или OpenCV без отдельного плана миграции.

---

## Текущее ограничение

High-resolution YOLO чувствителен к сильному изменению ориентации домино. Для production модель нужно дообучить на разных углах поворота, положениях объекта, изменениях масштаба и освещения.

Следующие шаги для anomaly detection — расширение GOOD bank и калибровка threshold на большем наборе GOOD / DEFECT примеров.
