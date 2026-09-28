from pathlib import Path

from fastapi import APIRouter, HTTPException, status
from fastapi.responses import FileResponse


router = APIRouter(prefix="/api/v1/cameras", tags=["cameras"])
SOURCE0_SNAPSHOT_PATH = Path(__file__).resolve().parents[3] / "runtime" / "source0_snapshot.jpg"
SNAPSHOT_PATH = Path(__file__).resolve().parents[3] / "runtime" / "source1_snapshot.jpg"


@router.get("/source0/snapshot.jpg", response_class=FileResponse)
def get_source0_snapshot() -> FileResponse:
    if not SOURCE0_SNAPSHOT_PATH.is_file():
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="source0 snapshot is not available")
    return FileResponse(SOURCE0_SNAPSHOT_PATH, media_type="image/jpeg")


@router.get("/source1/snapshot.jpg", response_class=FileResponse)
def get_source1_snapshot() -> FileResponse:
    if not SNAPSHOT_PATH.is_file():
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="source1 snapshot is not available")
    return FileResponse(SNAPSHOT_PATH, media_type="image/jpeg")