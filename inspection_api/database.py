from __future__ import annotations

import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from sqlalchemy import DateTime, Float, Integer, JSON, String, create_engine, desc, func, select
from sqlalchemy.dialects.postgresql import JSONB
from sqlalchemy.engine import Engine
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import DeclarativeBase, Mapped, Session, mapped_column, sessionmaker
from sqlalchemy.pool import StaticPool

from .models import InspectionMetadata

JsonColumn = JSON().with_variant(JSONB, "postgresql")


class Base(DeclarativeBase):
    pass


class Inspection(Base):
    __tablename__ = "inspections"

    inspection_id: Mapped[str] = mapped_column(String(128), primary_key=True)
    schema_version: Mapped[str] = mapped_column(String(16), nullable=False)
    device_id: Mapped[str] = mapped_column(String(128), nullable=False, index=True)
    timestamp: Mapped[datetime] = mapped_column(DateTime(timezone=True), nullable=False, index=True)
    result: Mapped[str] = mapped_column(String(16), nullable=False, index=True)
    detection_class: Mapped[str] = mapped_column(String(128), nullable=False)
    detection_confidence: Mapped[float] = mapped_column(Float, nullable=False)
    detection_bbox: Mapped[list[float]] = mapped_column(JsonColumn, nullable=False)
    anomaly_model: Mapped[str] = mapped_column(String(128), nullable=False)
    anomaly_embedding_dim: Mapped[int] = mapped_column(Integer, nullable=False)
    anomaly_matching: Mapped[str] = mapped_column(String(256), nullable=False)
    anomaly_threshold: Mapped[float] = mapped_column(Float, nullable=False)
    ignore_border_patches: Mapped[int] = mapped_column(Integer, nullable=False)
    score_min: Mapped[float] = mapped_column(Float, nullable=False)
    score_mean: Mapped[float] = mapped_column(Float, nullable=False)
    score_p95: Mapped[float] = mapped_column(Float, nullable=False)
    score_max: Mapped[float] = mapped_column(Float, nullable=False)
    anomaly_grid: Mapped[dict[str, Any]] = mapped_column(JsonColumn, nullable=False)
    anomaly_scores: Mapped[list[list[float]]] = mapped_column(JsonColumn, nullable=False)
    anomaly_regions: Mapped[list[dict[str, Any]]] = mapped_column(JsonColumn, nullable=False)
    sharpness: Mapped[float] = mapped_column(Float, nullable=False)
    brightness: Mapped[float] = mapped_column(Float, nullable=False)
    crop_width: Mapped[int] = mapped_column(Integer, nullable=False)
    crop_height: Mapped[int] = mapped_column(Integer, nullable=False)
    artifacts: Mapped[dict[str, Any]] = mapped_column(JsonColumn, nullable=False)
    storage_dir: Mapped[str] = mapped_column(String(1024), nullable=False)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), nullable=False, default=lambda: datetime.now(timezone.utc), server_default=func.now(), index=True
    )


class DatabaseConfigurationError(Exception):
    pass


class InspectionAlreadyExistsInDatabaseError(Exception):
    pass


class InspectionRepository:
    def __init__(self, database_url: str):
        if not database_url:
            raise DatabaseConfigurationError("INSPECTION_DATABASE_URL is required")
        engine_options: dict[str, Any] = {}
        if database_url.startswith("sqlite"):
            engine_options = {
                "connect_args": {"check_same_thread": False},
                "poolclass": StaticPool,
            }
        self.engine: Engine = create_engine(database_url, **engine_options)
        self.session_factory = sessionmaker(bind=self.engine, expire_on_commit=False)

    @classmethod
    def from_environment(cls) -> "InspectionRepository":
        database_url = os.getenv("INSPECTION_DATABASE_URL", "")
        if not database_url.startswith("postgresql+psycopg://"):
            raise DatabaseConfigurationError(
                "INSPECTION_DATABASE_URL must use postgresql+psycopg://"
            )
        return cls(database_url)

    def initialize(self) -> None:
        Base.metadata.create_all(self.engine)

    def dispose(self) -> None:
        self.engine.dispose()

    def exists(self, inspection_id: str) -> bool:
        with self.session_factory() as session:
            return session.get(Inspection, inspection_id) is not None

    def add(self, metadata: InspectionMetadata, storage_dir: str, saved_files: list[str]) -> Inspection:
        anomaly = metadata.anomaly
        artifact_paths = {
            field: str(Path(storage_dir) / filename)
            for field, filename in metadata.artifacts.model_dump(exclude_none=True).items()
            if filename in saved_files
        }
        row = Inspection(
            inspection_id=metadata.inspection_id,
            schema_version=metadata.schema_version,
            device_id=metadata.device_id,
            timestamp=metadata.timestamp,
            result=metadata.result,
            detection_class=metadata.detection.class_name,
            detection_confidence=metadata.detection.confidence,
            detection_bbox=metadata.detection.bbox,
            anomaly_model=anomaly.model,
            anomaly_embedding_dim=anomaly.embedding_dim,
            anomaly_matching=anomaly.matching,
            anomaly_threshold=anomaly.threshold,
            ignore_border_patches=anomaly.ignore_border_patches,
            score_min=anomaly.score_min,
            score_mean=anomaly.score_mean,
            score_p95=anomaly.score_p95,
            score_max=anomaly.score_max,
            anomaly_grid=anomaly.grid.model_dump(),
            anomaly_scores=anomaly.scores,
            anomaly_regions=[region.model_dump() for region in anomaly.regions],
            sharpness=metadata.quality.sharpness,
            brightness=metadata.quality.brightness,
            crop_width=metadata.quality.crop_width,
            crop_height=metadata.quality.crop_height,
            artifacts=artifact_paths,
            storage_dir=storage_dir,
        )
        try:
            with self.session_factory.begin() as session:
                session.add(row)
        except IntegrityError as error:
            raise InspectionAlreadyExistsInDatabaseError from error
        return row

    def get(self, inspection_id: str) -> Inspection | None:
        with self.session_factory() as session:
            return session.get(Inspection, inspection_id)

    def list(self, result: str | None, device_id: str | None, limit: int, offset: int) -> list[Inspection]:
        statement = select(Inspection)
        if result is not None:
            statement = statement.where(Inspection.result == result)
        if device_id is not None:
            statement = statement.where(Inspection.device_id == device_id)
        statement = statement.order_by(desc(Inspection.timestamp), desc(Inspection.inspection_id))
        statement = statement.limit(limit).offset(offset)
        with self.session_factory() as session:
            return list(session.scalars(statement))


def inspection_to_dict(row: Inspection) -> dict[str, Any]:
    return {
        "inspection_id": row.inspection_id,
        "schema_version": row.schema_version,
        "device_id": row.device_id,
        "timestamp": row.timestamp,
        "result": row.result,
        "detection": {
            "class": row.detection_class,
            "confidence": row.detection_confidence,
            "bbox": row.detection_bbox,
        },
        "anomaly": {
            "model": row.anomaly_model,
            "embedding_dim": row.anomaly_embedding_dim,
            "matching": row.anomaly_matching,
            "threshold": row.anomaly_threshold,
            "ignore_border_patches": row.ignore_border_patches,
            "score_min": row.score_min,
            "score_mean": row.score_mean,
            "score_p95": row.score_p95,
            "score_max": row.score_max,
            "grid": row.anomaly_grid,
            "scores": row.anomaly_scores,
            "regions": row.anomaly_regions,
        },
        "quality": {
            "sharpness": row.sharpness,
            "brightness": row.brightness,
            "crop_width": row.crop_width,
            "crop_height": row.crop_height,
        },
        "artifacts": row.artifacts,
        "storage_dir": row.storage_dir,
        "created_at": row.created_at,
    }
