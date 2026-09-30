import json

import pytest
from fastapi.testclient import TestClient

from inspection_api.app import app
from inspection_api.database import InspectionRepository


@pytest.fixture
def client(tmp_path, monkeypatch):
    monkeypatch.setenv("INSPECTION_DATA_DIR", str(tmp_path / "files"))
    repository = InspectionRepository("sqlite+pysqlite://")
    repository.initialize()
    app.state.inspection_repository = repository
    with TestClient(app) as test_client:
        yield test_client
    app.state.inspection_repository = None
    repository.dispose()


def payload(result="DEFECT", inspection_id="experiment_01_defect_test", timestamp="2026-09-29T23:53:31+05:00"):
    return {
        "schema_version": "1.0",
        "inspection_id": inspection_id,
        "device_id": "jetson_nano_01",
        "timestamp": timestamp,
        "result": result,
        "detection": {"class": "dm", "confidence": 0.882, "bbox": [1439.3, 1010.6, 1708.8, 1595.3]},
        "anomaly": {
            "model": "dino_vits16", "embedding_dim": 384, "matching": "all_to_all_good_bank",
            "threshold": 0.40, "ignore_border_patches": 1,
            "score_min": 0.04, "score_mean": 0.17, "score_p95": 0.30, "score_max": 0.52,
            "grid": {"rows": 2, "cols": 3, "patch_count": 6},
            "scores": [[0.1, 0.2, 0.3], [0.4, 0.5, 0.6]],
            "regions": [{"rank": 1, "row": 1, "col": 1, "score": 0.6, "bbox_crop": [80, 434, 100, 478], "center_crop": [90, 456]}],
        },
        "quality": {"sharpness": 34.9, "brightness": 112.1, "crop_width": 280, "crop_height": 608},
        "artifacts": {"full": "01_full.jpg"},
    }


def post(client, data, files=None):
    return client.post("/api/v1/inspections", data={"metadata": json.dumps(data)}, files=files or {})


def test_health(client):
    assert client.get("/health").json() == {"status": "ok"}


def test_cors_allows_local_vite_origin(client):
    response = client.options(
        "/api/v1/inspections",
        headers={
            "Origin": "http://localhost:5173",
            "Access-Control-Request-Method": "GET",
        },
    )
    assert response.status_code == 200
    assert response.headers["access-control-allow-origin"] == "http://localhost:5173"


def test_valid_defect_is_saved(client, tmp_path):
    response = post(client, payload())
    assert response.status_code == 201
    assert response.json()["status"] == "accepted"
    assert (tmp_path / "files" / "experiment_01_defect_test" / "inspection_result.json").is_file()


def test_valid_good_is_accepted(client):
    response = post(client, payload("GOOD", "experiment_01_good_test"))
    assert response.status_code == 201
    assert response.json()["result"] == "GOOD"


def test_invalid_anomaly_map_is_rejected(client):
    data = payload()
    data["anomaly"]["scores"] = [[0.1, 0.2], [0.3, 0.4]]
    assert post(client, data).status_code == 422


def test_invalid_result_is_rejected(client):
    data = payload()
    data["result"] = "UNKNOWN"
    assert post(client, data).status_code == 422


def test_multipart_image_is_saved(client, tmp_path):
    data = payload(inspection_id="experiment_image_test")
    response = post(client, data, {"full": ("from_jetson.jpg", b"jpeg-bytes", "image/jpeg")})
    assert response.status_code == 201
    assert (tmp_path / "files" / "experiment_image_test" / "01_full.jpg").read_bytes() == b"jpeg-bytes"


def test_path_traversal_is_rejected(client):
    data = payload(inspection_id="../escape")
    assert post(client, data).status_code == 422


def test_duplicate_inspection_id_returns_409(client):
    assert post(client, payload()).status_code == 201
    assert post(client, payload()).status_code == 409


def test_get_by_inspection_id_returns_metadata_scores_and_paths(client):
    data = payload()
    assert post(client, data, {"full": ("from_jetson.jpg", b"jpeg-bytes", "image/jpeg")}).status_code == 201
    response = client.get("/api/v1/inspections/experiment_01_defect_test")
    assert response.status_code == 200
    body = response.json()
    assert body["anomaly"]["scores"] == data["anomaly"]["scores"]
    assert body["anomaly"]["regions"] == data["anomaly"]["regions"]
    assert body["artifacts"]["full"].endswith("experiment_01_defect_test/01_full.jpg")
    assert body["storage_dir"].endswith("experiment_01_defect_test")


def test_list_and_result_filter(client):
    assert post(client, payload("DEFECT", "defect_one", "2026-09-29T23:53:01+05:00")).status_code == 201
    assert post(client, payload("GOOD", "good_one", "2026-09-29T23:53:02+05:00")).status_code == 201
    assert post(client, payload("DEFECT", "defect_two", "2026-09-29T23:53:03+05:00")).status_code == 201

    all_rows = client.get("/api/v1/inspections?limit=10").json()
    defect_rows = client.get("/api/v1/inspections?result=DEFECT&limit=10").json()
    assert [row["inspection_id"] for row in all_rows] == ["defect_two", "good_one", "defect_one"]
    assert {row["inspection_id"] for row in defect_rows} == {"defect_one", "defect_two"}
    assert all(row["result"] == "DEFECT" for row in defect_rows)



def test_registered_jpeg_artifact_is_served(client):
    assert post(client, payload(), {"full": ("from_jetson.jpg", b"jpeg-bytes", "image/jpeg")}).status_code == 201
    response = client.get("/api/v1/inspections/experiment_01_defect_test/artifacts/full")
    assert response.status_code == 200
    assert response.content == b"jpeg-bytes"
    assert response.headers["content-type"].startswith("image/jpeg")


def test_registered_png_artifact_is_served(client):
    data = payload(inspection_id="experiment_png_test")
    data["artifacts"]["heatmap"] = "heatmap.png"
    assert post(client, data, {"heatmap": ("from_jetson.png", b"png-bytes", "image/png")}).status_code == 201
    response = client.get("/api/v1/inspections/experiment_png_test/artifacts/heatmap")
    assert response.status_code == 200
    assert response.headers["content-type"].startswith("image/png")


def test_unknown_artifact_and_inspection_are_not_served(client):
    assert post(client, payload()).status_code == 201
    assert client.get("/api/v1/inspections/experiment_01_defect_test/artifacts/full").status_code == 404
    assert client.get("/api/v1/inspections/not_found/artifacts/full").status_code == 404


def test_artifact_path_traversal_and_unregistered_file_are_rejected(client, tmp_path):
    assert post(client, payload()).status_code == 201
    directory = tmp_path / "files" / "experiment_01_defect_test"
    (directory / "private.jpg").write_bytes(b"private")
    assert client.get("/api/v1/inspections/experiment_01_defect_test/artifacts/..%2Fprivate.jpg").status_code == 404
    assert client.get("/api/v1/inspections/experiment_01_defect_test/artifacts/private.jpg").status_code == 404
