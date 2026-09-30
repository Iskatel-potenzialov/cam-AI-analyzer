# Model files

The binary model files are intentionally **not included** in this archive.

Expected paths on the Jetson Nano:

```text
/home/nvideo/domino_inspection/models/
├── best_domino_280424.engine
└── libmyplugins.so
```

## YOLO TensorRT

`best_domino_280424.engine` is the TensorRT engine used by the current detector.
The detector expects one class:

```text
dm
```

The engine is loaded by `src/detector_legacy.py` together with the custom plugin library `libmyplugins.so`.

The current runtime environment is:

- CUDA 10.2
- TensorRT 8.2.1.8
- Jetson Nano / aarch64

TensorRT engine files are not generally portable across arbitrary TensorRT versions or device families. Rebuild the engine when migrating the runtime or hardware.

## DINO

The anomaly stage uses:

```text
facebookresearch/dino:main
dino_vits16
```

The current Jetson already has the model in the Torch Hub cache. The first run on a clean device may require network access or a pre-populated cache.
