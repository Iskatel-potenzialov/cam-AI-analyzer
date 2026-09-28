from datetime import datetime, timezone

from fastapi import APIRouter, HTTPException, status

from ..live_state import live_state_store
from ..schemas import LiveStateCreate, LiveStateRead


router = APIRouter(prefix="/api/v1/live-state", tags=["live-state"])


@router.post("", response_model=LiveStateRead, status_code=status.HTTP_200_OK)
def replace_live_state(snapshot: LiveStateCreate):
 state = LiveStateRead(**snapshot.model_dump(), updated_at=datetime.now(timezone.utc))
 return live_state_store.replace(state)


@router.get("/{camera_id}", response_model=LiveStateRead)
def get_live_state(camera_id: str):
 snapshot = live_state_store.get(camera_id)
 if snapshot is None:
  raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail=f"live state not found for camera_id: {camera_id}")
 return snapshot