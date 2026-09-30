import json
import os
from pathlib import Path
from typing import Annotated

from fastapi import Depends, FastAPI, File, Form, HTTPException, Request, UploadFile
from fastapi.responses import FileResponse
from fastapi.middleware.cors import CORSMiddleware
from pydantic import ValidationError
from sqlalchemy.exc import SQLAlchemyError

from .database import (
    DatabaseConfigurationError,
    InspectionAlreadyExistsInDatabaseError,
    InspectionRepository,
    inspection_to_dict,
)
from .models import InspectionMetadata
from .storage import (
    InspectionAlreadyExistsError,
    InvalidUploadError,
    inspection_storage_dir,
    remove_inspection_directory,
    save_inspection,
)

DEFAULT_CORS_ORIGINS = (
    "http://localhost:5173",
    "http://127.0.0.1:5173",
)


def cors_origins() -> list[str]:
    configured = os.getenv("INSPECTION_CORS_ORIGINS")
    if configured is None:
        return list(DEFAULT_CORS_ORIGINS)
    return [origin.strip() for origin in configured.split(",") if origin.strip()]


app = FastAPI(title="Inspection Intake API", version="1.0")
app.add_middleware(
    CORSMiddleware,
    allow_origins=cors_origins(),
    allow_credentials=False,
    allow_methods=["GET", "POST"],
    allow_headers=["Content-Type"],
)


def database_unavailable(error: SQLAlchemyError) -> HTTPException:
    return HTTPException(
        status_code=503,
        detail=f"inspection database unavailable: {error}",
    )


def get_repository(request: Request) -> InspectionRepository:
    repository = getattr(request.app.state, "inspection_repository", None)
    if repository is not None:
        return repository
    try:
        repository = InspectionRepository.from_environment()
        repository.initialize()
    except (DatabaseConfigurationError, SQLAlchemyError) as error:
        raise HTTPException(status_code=503, detail=f"inspection database unavailable: {error}") from error
    request.app.state.inspection_repository = repository
    return repository


Repository = Annotated[InspectionRepository, Depends(get_repository)]


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.post("/api/v1/inspections", status_code=201)
async def create_inspection(
    repository: Repository,
    metadata: str = Form(...),
    search_full: UploadFile | None = File(None),
    search_bbox: UploadFile | None = File(None),
    full: UploadFile | None = File(None),
    full_bbox: UploadFile | None = File(None),
    crop: UploadFile | None = File(None),
    heatmap: UploadFile | None = File(None),
    overlay: UploadFile | None = File(None),
    anomalies: UploadFile | None = File(None)
) -> dict[str, object]:
    try:
        parsed = InspectionMetadata.model_validate_json(metadata)
    except ValidationError as error:
        raise HTTPException(status_code=422, detail=json.loads(error.json())) from error

    try:
        if repository.exists(parsed.inspection_id):
            raise HTTPException(status_code=409, detail="inspection_id already exists")
    except SQLAlchemyError as error:
        raise database_unavailable(error) from error

    uploads = {
        "search_full": search_full,
        "search_bbox": search_bbox,
        "full": full,
        "full_bbox": full_bbox,
        "crop": crop,
        "heatmap": heatmap,
        "overlay": overlay,
        "anomalies": anomalies,
    }
    try:
        saved = await save_inspection(parsed, uploads)
    except InspectionAlreadyExistsError:
        raise HTTPException(status_code=409, detail="inspection_id already exists") from None
    except InvalidUploadError as error:
        raise HTTPException(status_code=422, detail=str(error)) from error

    storage_dir = str(inspection_storage_dir(parsed.inspection_id))
    try:
        repository.add(parsed, storage_dir, saved)
    except InspectionAlreadyExistsInDatabaseError:
        remove_inspection_directory(parsed.inspection_id)
        raise HTTPException(status_code=409, detail="inspection_id already exists") from None
    except SQLAlchemyError as error:
        remove_inspection_directory(parsed.inspection_id)
        raise HTTPException(status_code=503, detail=f"inspection database write failed: {error}") from error

    return {
        "status": "accepted",
        "inspection_id": parsed.inspection_id,
        "result": parsed.result,
        "saved_files": saved,
    }


@app.get("/api/v1/inspections/{inspection_id}")
def get_inspection(inspection_id: str, repository: Repository) -> dict[str, object]:
    try:
        inspection = repository.get(inspection_id)
    except SQLAlchemyError as error:
        raise database_unavailable(error) from error
    if inspection is None:
        raise HTTPException(status_code=404, detail="inspection not found")
    return inspection_to_dict(inspection)


@app.get("/api/v1/inspections/{inspection_id}/artifacts/{artifact_name}")
def get_inspection_artifact(
    inspection_id: str, artifact_name: str, repository: Repository
) -> FileResponse:
    try:
        inspection = repository.get(inspection_id)
    except SQLAlchemyError as error:
        raise database_unavailable(error) from error
    if inspection is None:
        raise HTTPException(status_code=404, detail="inspection not found")

    stored_path = inspection.artifacts.get(artifact_name)
    if stored_path is None:
        raise HTTPException(status_code=404, detail="artifact not found")

    inspection_dir = inspection_storage_dir(inspection_id).resolve()
    artifact_path = Path(stored_path).resolve()
    if artifact_path.parent != inspection_dir or not artifact_path.is_file():
        raise HTTPException(status_code=404, detail="artifact not found")
    return FileResponse(artifact_path)


@app.get("/api/v1/inspections")
def list_inspections(
    repository: Repository,
    result: str | None = None,
    device_id: str | None = None,
    limit: int = 100,
    offset: int = 0,
) -> list[dict[str, object]]:
    if result is not None and result not in {"GOOD", "DEFECT"}:
        raise HTTPException(status_code=422, detail="result must be GOOD or DEFECT")
    if not 1 <= limit <= 1000:
        raise HTTPException(status_code=422, detail="limit must be between 1 and 1000")
    if offset < 0:
        raise HTTPException(status_code=422, detail="offset must be non-negative")
    try:
        inspections = repository.list(result, device_id, limit, offset)
    except SQLAlchemyError as error:
        raise database_unavailable(error) from error
    return [inspection_to_dict(row) for row in inspections]
