from threading import Lock

from .schemas import LiveStateRead


class LiveStateStore:
 def __init__(self):
  self._states: dict[str, LiveStateRead] = {}
  self._lock = Lock()

 def replace(self, snapshot: LiveStateRead) -> LiveStateRead:
  with self._lock:
   self._states[snapshot.camera_id] = snapshot.model_copy(deep=True)
   return self._states[snapshot.camera_id].model_copy(deep=True)

 def get(self, camera_id: str) -> LiveStateRead | None:
  with self._lock:
   snapshot = self._states.get(camera_id)
   return snapshot.model_copy(deep=True) if snapshot is not None else None


live_state_store = LiveStateStore()