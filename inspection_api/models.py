from __future__ import annotations

import math
import re
from datetime import datetime
from pathlib import Path
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

_SAFE_ID = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$")
_SAFE_SUFFIXES = {".jpg", ".jpeg", ".png"}


def _finite(value: float, name: str) -> float:
    if not math.isfinite(value):
        raise ValueError(f"{name} must be finite")
    return value


class Detection(BaseModel):
    model_config = ConfigDict(extra="forbid")
    class_name: str = Field(alias="class", min_length=1)
    confidence: float = Field(ge=0, le=1)
    bbox: list[float] = Field(min_length=4, max_length=4)

    @field_validator("confidence")
    @classmethod
    def finite_confidence(cls, value: float) -> float:
        return _finite(value, "detection confidence")

    @field_validator("bbox")
    @classmethod
    def finite_bbox(cls, value: list[float]) -> list[float]:
        for item in value:
            _finite(item, "detection bbox")
        return value


class Grid(BaseModel):
    model_config = ConfigDict(extra="forbid")
    rows: int = Field(ge=1)
    cols: int = Field(ge=1)
    patch_count: int = Field(ge=1)


class Region(BaseModel):
    model_config = ConfigDict(extra="forbid")
    rank: int = Field(ge=1)
    row: int = Field(ge=0)
    col: int = Field(ge=0)
    score: float
    bbox_crop: list[float] = Field(min_length=4, max_length=4)
    center_crop: list[float] = Field(min_length=2, max_length=2)

    @field_validator("score")
    @classmethod
    def finite_score(cls, value: float) -> float:
        return _finite(value, "region score")

    @field_validator("bbox_crop", "center_crop")
    @classmethod
    def finite_coordinates(cls, value: list[float]) -> list[float]:
        for item in value:
            _finite(item, "region coordinate")
        return value


class Anomaly(BaseModel):
    model_config = ConfigDict(extra="forbid")
    model: str = Field(min_length=1)
    embedding_dim: int = Field(ge=1)
    matching: str = Field(min_length=1)
    threshold: float = Field(ge=0)
    ignore_border_patches: int = Field(ge=0)
    score_min: float
    score_mean: float
    score_p95: float
    score_max: float
    grid: Grid
    scores: list[list[float]]
    regions: list[Region] = Field(default_factory=list)

    @field_validator("threshold", "score_min", "score_mean", "score_p95", "score_max")
    @classmethod
    def finite_value(cls, value: float) -> float:
        return _finite(value, "anomaly value")

    @model_validator(mode="after")
    def valid_map(self) -> "Anomaly":
        if self.grid.patch_count != self.grid.rows * self.grid.cols:
            raise ValueError("anomaly patch_count must equal rows * cols")
        if len(self.scores) != self.grid.rows:
            raise ValueError("anomaly scores row count must match grid.rows")
        for row in self.scores:
            if len(row) != self.grid.cols:
                raise ValueError("each anomaly scores row must match grid.cols")
            for score in row:
                _finite(score, "anomaly score")
        for region in self.regions:
            if region.row >= self.grid.rows or region.col >= self.grid.cols:
                raise ValueError("anomaly region must be inside the grid")
        return self


class Quality(BaseModel):
    model_config = ConfigDict(extra="forbid")
    sharpness: float
    brightness: float
    crop_width: int = Field(ge=1)
    crop_height: int = Field(ge=1)

    @field_validator("sharpness", "brightness")
    @classmethod
    def finite_quality(cls, value: float) -> float:
        return _finite(value, "quality value")


class Artifacts(BaseModel):
    model_config = ConfigDict(extra="forbid")
    search_full: str | None = None
    search_bbox: str | None = None
    full: str | None = None
    full_bbox: str | None = None
    crop: str | None = None
    heatmap: str | None = None
    overlay: str | None = None
    anomalies: str | None = None

    @field_validator("*")
    @classmethod
    def safe_filename(cls, value: str | None) -> str | None:
        if value is None:
            return value
        path = Path(value)
        if not value or path.name != value or ".." in value or path.suffix.lower() not in _SAFE_SUFFIXES:
            raise ValueError("artifact filename must be a safe image basename")
        return value


class InspectionMetadata(BaseModel):
    model_config = ConfigDict(extra="forbid")
    schema_version: Literal["1.0"]
    inspection_id: str
    device_id: str = Field(min_length=1, max_length=128)
    timestamp: datetime
    result: Literal["GOOD", "DEFECT"]
    detection: Detection
    anomaly: Anomaly
    quality: Quality
    artifacts: Artifacts = Field(default_factory=Artifacts)

    @field_validator("inspection_id")
    @classmethod
    def safe_inspection_id(cls, value: str) -> str:
        value = value.strip()
        if not _SAFE_ID.fullmatch(value) or ".." in value:
            raise ValueError("inspection_id contains unsafe characters")
        return value

    @field_validator("device_id")
    @classmethod
    def non_blank_device_id(cls, value: str) -> str:
        value = value.strip()
        if not value:
            raise ValueError("device_id must not be blank")
        return value

    @field_validator("timestamp")
    @classmethod
    def aware_timestamp(cls, value: datetime) -> datetime:
        if value.tzinfo is None or value.utcoffset() is None:
            raise ValueError("timestamp must be timezone-aware")
        return value
