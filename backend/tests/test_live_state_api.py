from datetime import datetime
import time
from uuid import uuid4

import pytest
from fastapi.testclient import TestClient

from app.main import app

client = TestClient(app)


def snapshot(camera_id=None, *, red=1, yellow=1, green=1, people=None):
 if people is None:
  people = [
   {"track_id": 11, "zone": "RED", "confidence": 0.90},
   {"track_id": 12, "zone": "YELLOW", "confidence": 0.70},
   {"track_id": 13, "zone": "GREEN"},
  ]
 return {"camera_id": camera_id or f"source1-test-{uuid4()}", "counts": {"red": red, "yellow": yellow, "green": green}, "people": people}


def test_post_valid_and_get_returns_same_snapshot():
 payload = snapshot()
 posted = client.post("/api/v1/live-state", json=payload)
 assert posted.status_code == 200
 body = posted.json()
 assert body["camera_id"] == payload["camera_id"]
 assert body["counts"] == payload["counts"]
 expected_people = payload["people"][:2] + [{"track_id": 13, "zone": "GREEN", "confidence": None}]
 assert body["people"] == expected_people
 assert datetime.fromisoformat(body["updated_at"].replace("Z", "+00:00")).tzinfo is not None
 fetched = client.get(f"/api/v1/live-state/{payload['camera_id']}")
 assert fetched.status_code == 200
 assert fetched.json()["people"] == expected_people
 assert fetched.json() == body


def test_second_post_replaces_the_full_snapshot():
 camera_id = f"source1-test-{uuid4()}"
 first = client.post("/api/v1/live-state", json=snapshot(camera_id))
 assert first.status_code == 200
 first_updated_at = datetime.fromisoformat(first.json()["updated_at"].replace("Z", "+00:00"))
 time.sleep(0.001)
 replacement = snapshot(camera_id, red=0, yellow=0, green=0, people=[])
 response = client.post("/api/v1/live-state", json=replacement)
 assert response.status_code == 200
 assert response.json()["counts"] == replacement["counts"]
 assert response.json()["people"] == []
 assert datetime.fromisoformat(response.json()["updated_at"].replace("Z", "+00:00")) > first_updated_at
 assert client.get(f"/api/v1/live-state/{camera_id}").json()["people"] == []


def test_camera_snapshots_are_isolated():
 first_camera = f"source1-test-{uuid4()}"
 second_camera = f"source2-test-{uuid4()}"
 first = snapshot(first_camera, red=1, yellow=0, green=0, people=[{"track_id": 11, "zone": "RED", "confidence": 0.90}])
 second = snapshot(second_camera, red=0, yellow=0, green=1, people=[{"track_id": 22, "zone": "GREEN", "confidence": 0.70}])
 assert client.post("/api/v1/live-state", json=first).status_code == 200
 assert client.post("/api/v1/live-state", json=second).status_code == 200
 assert client.get(f"/api/v1/live-state/{first_camera}").json()["people"] == first["people"]
 assert client.get(f"/api/v1/live-state/{second_camera}").json()["people"] == second["people"]


def test_unknown_camera_returns_404():
 response = client.get(f"/api/v1/live-state/unknown-{uuid4()}")
 assert response.status_code == 404
 assert "live state not found" in response.json()["detail"]


@pytest.mark.parametrize("payload", [
 snapshot(red=-1),
 snapshot(people=[{"track_id": 11, "zone": "BLUE"}], red=0, yellow=0, green=1),
 snapshot(people=[{"track_id": 11, "zone": "RED", "confidence": 1.1}], red=1, yellow=0, green=0),
 snapshot(people=[{"track_id": 11, "zone": "RED", "confidence": -0.1}], red=1, yellow=0, green=0),
 snapshot(people=[{"track_id": 11, "zone": "RED"}, {"track_id": 11, "zone": "RED"}], red=2, yellow=0, green=0),
 snapshot(red=0, yellow=0, green=0),
])
def test_invalid_live_state_is_rejected(payload):
 assert client.post("/api/v1/live-state", json=payload).status_code == 422


def test_empty_people_with_zero_counts_is_valid():
 response = client.post("/api/v1/live-state", json=snapshot(red=0, yellow=0, green=0, people=[]))
 assert response.status_code == 200
 assert response.json()["people"] == []
