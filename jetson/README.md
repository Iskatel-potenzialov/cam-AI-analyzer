# Инспекция домино на Jetson Nano

Система визуального контроля поверхности домино на NVIDIA Jetson Nano.

Jetson локально получает изображение с CSI-камеры, находит домино через YOLO, делает high-resolution снимок, выделяет объект и анализирует его поверхность через DINO. Результат инспекции `GOOD / DEFECT` вместе с изображениями и anomaly scores (оценками аномалии) отправляется на Ubuntu, сохраняется в PostgreSQL и отображается в веб-интерфейсе.

Case 3 работает отдельно от основного DeepStream-конвейера Case 1 / Case 2. Raw video (необработанный видеопоток) на Ubuntu не передаётся — inference (запуск нейросетей) выполняется непосредственно на Jetson Nano.

---

## Что умеет система

| Этап | Что происходит |
|---|---|
| Поиск объекта | YOLO находит домино в кадре `1280×720` |
| Стабилизация | система ждёт несколько стабильных детекций подряд |
| High-resolution capture | камера переключается на `3264×2464` |
| Точная локализация | YOLO повторно определяет bbox уже на high-resolution кадре |
| Выбор кадра | из нескольких кадров выбирается наиболее резкий |
| Crop | формируется изображение только с домино |
| Анализ поверхности | DINO сравнивает поверхность с GOOD-эталоном |
| Результат | формируется `GOOD` или `DEFECT` |
| Отправка | JSON и изображения передаются в Inspection API |
| Веб-интерфейс | оператор видит результат и изображения инспекции |

---

## Архитектура

```text
CSI Camera
    │
    ▼
1280×720
YOLO — поиск домино
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
YOLO
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
анализ поверхности
    │
    ▼
GOOD / DEFECT
    │
    ▼
inspection_result.json + изображения
    │
    ▼
HTTP
    │
    ▼
Inspection API / Ubuntu
    │
    ├── PostgreSQL
    └── файлы инспекции
            │
            ▼
        React frontend
```

YOLO и DINO специально работают в разных процессах, чтобы одновременно не занимать память Jetson Nano.

---

# Как проходит инспекция

## 1. Поиск домино

Сначала камера работает в режиме:

```text
1280×720
```

YOLO ищет объект класса `dm`.

После детекции система получает:

- confidence (уверенность модели);
- bbox (координаты объекта);
- положение объекта в кадре.

Пример:

```text
STABILIZE: 1/5
STABILIZE: 2/5
STABILIZE: 3/5
STABILIZE: 4/5
STABILIZE: 5/5
```

Объект считается готовым к инспекции только после нескольких стабильных детекций подряд.

Это уменьшает вероятность анализа движущегося или ещё не установленного объекта.

---

## 2. High-resolution capture

После стабилизации low-resolution камера закрывается и открывается режим:

```text
3264×2464
```

Первые кадры используются для warm-up (стабилизации камеры).

После этого снимается несколько high-resolution кадров.

На каждом из них YOLO повторно определяет положение домино.

Повторная детекция нужна потому, что `1280×720` и `3264×2464` используют разные sensor modes (режимы сенсора), поэтому bbox из первого режима нельзя считать точным bbox для второго.

---

## 3. Выбор лучшего кадра

Для каждого успешно найденного домино вычисляются:

- sharpness (резкость);
- brightness (яркость).

Из нескольких кандидатов выбирается наиболее резкий кадр.

После этого сохраняются:

```text
01_full.jpg
02_full_bbox.jpg
03_crop.jpg
```

`03_crop.jpg` — основной вход для анализа поверхности.

---

# YOLO

Для поиска домино используется TensorRT engine.

Основные задачи YOLO в Case 3 разные на двух этапах:

```text
1280×720
→ найти объект
→ убедиться, что он стабилен

3264×2464
→ точно найти объект на качественном кадре
→ сформировать crop для DINO
```

YOLO отвечает только за локализацию домино.

Определением дефекта занимается отдельная модель DINO.

---

# DINO anomaly detection

После завершения YOLO-процесса запускается отдельный процесс с:

```text
DINO ViT-S/16
```

В DINO передаётся:

```text
03_crop.jpg
```

Изображение приводится к:

```text
224×224
```

При patch size (размере патча) `16×16` получается:

```text
14 × 14 = 196 patches
```

Для каждого patch DINO формирует embedding (вектор признаков) размерности:

```text
384
```

Итог:

```text
196 × 384
```

---

## Сравнение с GOOD-эталоном

Для исправного домино заранее формируется GOOD reference.

Каждый patch тестового изображения сравнивается со всеми patch embeddings GOOD-эталона.

```text
test patch
    ↓
все GOOD patches
    ↓
максимальная cosine similarity
    ↓
anomaly score
```

Используется:

```text
anomaly score = 1 - best cosine similarity
```

Чем больше значение, тем сильнее участок отличается от исправного образца.

Текущий экспериментальный threshold (порог):

```text
0.40
```

Если значимых областей выше порога нет:

```text
GOOD
```

Если они есть:

```text
DEFECT
```

---

## Результаты DINO

Для каждой инспекции формируются:

```text
07_anomaly_heatmap.jpg
08_anomaly_overlay.jpg
09_top_anomalies_filtered.jpg
```

`09_top_anomalies_filtered.jpg` показывает исходный crop и области поверхности, которые превысили установленный threshold.

Также сохраняется полная числовая карта:

```text
14 × 14 anomaly scores
```

Поэтому frontend в дальнейшем сможет менять порог отображения без повторного запуска DINO.

---

# Memory-safe архитектура

Jetson Nano имеет ограниченный объём памяти.

YOLO использует TensorRT, а DINO — PyTorch.

Если загрузить обе модели одновременно в один Python-процесс, Jetson испытывает сильный memory pressure (нехватку памяти).

Поэтому pipeline разделён:

```text
inspection_pipeline_memory_safe.py
        │
        ▼
YOLO process
        │
        ▼
process полностью завершается
        │
        ▼
DINO process
        │
        ▼
process полностью завершается
```

Главный controller (управляющий процесс) сам не загружает TensorRT и PyTorch модели.

---

# Результат инспекции

Для каждой проверки создаётся:

```text
inspection_result.json
```

Он содержит:

- `inspection_id`;
- `device_id`;
- время инспекции;
- `GOOD / DEFECT`;
- YOLO confidence;
- bbox;
- DINO threshold;
- `score_min`;
- `score_mean`;
- `score_p95`;
- `score_max`;
- полную карту `14×14`;
- найденные anomaly regions;
- sharpness;
- brightness;
- список изображений.

Пример набора файлов:

```text
results/
└── production/
    └── inspection_.../
        ├── 00_search_full.jpg
        ├── 00_search_bbox.jpg
        ├── 01_full.jpg
        ├── 02_full_bbox.jpg
        ├── 03_crop.jpg
        ├── 07_anomaly_heatmap.jpg
        ├── 08_anomaly_overlay.jpg
        ├── 09_top_anomalies_filtered.jpg
        ├── metadata.json
        └── inspection_result.json
```

---

# Отправка результата

После анализа Jetson отправляет результат на Ubuntu:

```text
POST /api/v1/inspections
```

Inspection API работает отдельно от API Case 1 / Case 2.

Текущая схема:

```text
Case 1 / Case 2
→ FastAPI :8000
→ cv_events
→ events

Case 3
→ Inspection API :8010
→ inspection_db
→ inspections
```

Две PostgreSQL базы являются независимыми.

---

# Backend

Inspection backend написан на FastAPI.

Он отвечает за:

- приём результатов Jetson;
- проверку структуры `inspection_result.json`;
- сохранение metadata в PostgreSQL;
- сохранение изображений;
- выдачу списка инспекций;
- выдачу конкретной инспекции;
- безопасную выдачу изображений.

Основные endpoints:

```text
GET  /health

POST /api/v1/inspections

GET  /api/v1/inspections
GET  /api/v1/inspections/{inspection_id}

GET  /api/v1/inspections/{inspection_id}/artifacts/{artifact_name}
```

Изображения не публикуются через общий static directory.

Backend разрешает получить только artifact, зарегистрированный для конкретной inspection.

---

# Frontend

Case 3 добавлен в существующий React frontend отдельной вкладкой:

```text
Инспекции домино
```

Frontend показывает:

- список инспекций;
- `GOOD / DEFECT`;
- дату и время;
- устройство;
- YOLO confidence;
- anomaly score;
- quality metrics;
- crop;
- bbox;
- heatmap;
- overlay;
- найденные anomaly regions.

Frontend только отображает результаты.

Он не запускает инспекцию на Jetson.

---

# Запуск

Тестовый запуск одной полной инспекции:

```bash
cd /home/nvideo/domino_inspection
python3 src/inspection_pipeline_memory_safe.py --once
```

Pipeline выполняет:

```text
SEARCH
→ STABILIZE
→ HIGH-RES
→ YOLO
→ CROP
→ DINO
→ JSON
→ HTTP POST
→ exit
```

При ошибке `--once` не запускает автоматический повтор.

---

# Production trigger

Сейчас для тестирования pipeline запускается вручную.

В производственной системе запуск должен происходить по внешнему сигналу, например:

```text
PLC
датчик
кнопка
сигнал конвейера
управляющая система
```

Логика анализа Jetson от этого не меняется.

Меняется только способ запуска одной инспекции.

---

# Технологический стек

### Computer Vision

- YOLOv5
- TensorRT
- DINO ViT-S/16
- OpenCV

### NVIDIA / Jetson

- NVIDIA Jetson Nano
- CUDA
- TensorRT
- CSI Camera
- GStreamer / Argus

### ML

- PyTorch
- torchvision
- NumPy

### Backend

- Python
- FastAPI
- Pydantic
- PostgreSQL

### Frontend

- React
- TypeScript
- Vite

---

# Текущее ограничение

Во время тестирования обнаружено, что high-resolution YOLO чувствителен к сильному изменению ориентации (повороту) домино.

При повороте объекта low-resolution detector продолжал его находить, а high-resolution detector мог потерять детекцию.

После возвращения объекта ближе к ориентации обучающих данных high-resolution YOLO снова стабильно находил домино.

Для production модель нужно дообучить на:

- разных углах поворота;
- разных положениях;
- реальных high-resolution кадрах;
- изменениях масштаба;
- изменениях освещения.

---

# Что дальше

## Улучшение YOLO

Собрать дополнительный датасет с реальной CSI-камеры и дообучить модель на допустимых положениях объекта.

---

## Расширение GOOD bank

Сейчас anomaly detection проверен на небольшом наборе GOOD reference.

Дальше можно добавить несколько исправных образцов, чтобы система была устойчивее к естественным различиям между изделиями.

---

## Калибровка threshold

Текущий:

```text
0.40
```

является экспериментальным.

Для production threshold нужно определить на расширенном наборе:

```text
GOOD
+
DEFECT
```

и оценить количество false positive / false negative.

---

# Ключевая идея Case 3

Case 3 разделяет задачи между двумя моделями:

```text
YOLO
→ где находится изделие

DINO
→ отличается ли его поверхность от исправной
```

Jetson выполняет весь inference локально.

На Ubuntu передаётся уже готовый результат:

```text
inspection
→ metadata
→ anomaly scores
→ images
→ GOOD / DEFECT
```

Backend отвечает за хранение.

Frontend — за отображение результата оператору.

Такой подход позволяет использовать Jetson Nano как автономный edge-узел контроля качества и не передавать на сервер постоянный видеопоток.
