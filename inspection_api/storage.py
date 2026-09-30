from __future__ import annotations

import json
import os
import shutil
from pathlib import Path

from fastapi import UploadFile

from .models import InspectionMetadata

ARTIFACT_FIELDS = ("search_full", "search_bbox", "full", "full_bbox", "crop", "heatmap", "overlay", "anomalies")
_SUFFIX_FOR_TYPE = {"image/jpeg": ".jpg", "image/png": ".png"}


class InspectionAlreadyExistsError(Exception):
    pass


class InvalidUploadError(Exception):
    pass


def get_storage_root() -> Path:
    configured = os.getenv("INSPECTION_DATA_DIR")
    return Path(configured).resolve() if configured else (Path(__file__).resolve().parent.parent / "data" / "inspections").resolve()


def _destination(directory: Path, metadata: InspectionMetadata, field: str, upload: UploadFile) -> Path:
    filename = getattr(metadata.artifacts, field)
    if filename is None:
        suffix = _SUFFIX_FOR_TYPE.get(upload.content_type or "")
        if suffix is None:
            raise InvalidUploadError("only JPEG and PNG uploads are accepted")
        filename = field + suffix
    target = (directory / filename).resolve()
    if target.parent != directory.resolve():
        raise InvalidUploadError("unsafe artifact filename")
    return target


async def _write_upload(upload: UploadFile, target: Path) -> None:
    if upload.content_type not in _SUFFIX_FOR_TYPE:
        raise InvalidUploadError("only JPEG and PNG uploads are accepted")
    temporary = target.with_name("." + target.name + ".upload")
    try:
        with temporary.open("wb") as output:
            while chunk := await upload.read(1024 * 1024):
                output.write(chunk)
        temporary.replace(target)
    finally:
        await upload.close()
        temporary.unlink(missing_ok=True)


async def save_inspection(metadata: InspectionMetadata, uploads: dict[str, UploadFile | None]) -> list[str]:
    root = get_storage_root()
    root.mkdir(parents=True, exist_ok=True)
    directory = (root / metadata.inspection_id).resolve()
    if directory.parent != root.resolve():
        raise InvalidUploadError("unsafe inspection directory")
    try:
        directory.mkdir()
    except FileExistsError as error:
        raise InspectionAlreadyExistsError from error
    try:
        result = directory / "inspection_result.json"
        result.write_text(json.dumps(metadata.model_dump(mode="json", by_alias=True), ensure_ascii=False, indent=2), encoding="utf-8")
        saved = [result.name]
        for field in ARTIFACT_FIELDS:
            upload = uploads[field]
            if upload is None:
                continue
            target = _destination(directory, metadata, field, upload)
            await _write_upload(upload, target)
            saved.append(target.name)
        return saved
    except Exception:
        shutil.rmtree(directory, ignore_errors=True)
        raise


def inspection_storage_dir(inspection_id: str) -> Path:
    root = get_storage_root()
    directory = (root / inspection_id).resolve()
    if directory.parent != root.resolve():
        raise InvalidUploadError("unsafe inspection directory")
    return directory


def remove_inspection_directory(inspection_id: str) -> None:
    directory = inspection_storage_dir(inspection_id)
    shutil.rmtree(directory, ignore_errors=True)
