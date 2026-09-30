# Inspection Intake API

Isolated FastAPI service for Jetson Nano inspection results. It does not import, start, or modify DeepStream or the existing Event API.

## PostgreSQL configuration

Production requires INSPECTION_DATABASE_URL. SQLite is not used by production code and is used only by unit tests through explicit repository injection.

Example:

    export INSPECTION_DATABASE_URL='postgresql+psycopg://inspection_app:password@127.0.0.1:5432/inspection_db'

Create the role and database manually on Ubuntu:

    sudo -u postgres createuser --pwprompt inspection_app
    sudo -u postgres createdb --owner=inspection_app inspection_db

The first repository initialization calls SQLAlchemy Base.metadata.create_all() and creates the inspections table. There is no Alembic migration in this isolated first version.

## Install and run

    cd /home/evgeny/CV/deepstream-car-tracking
    python3 -m venv inspection-api-venv
    source inspection-api-venv/bin/activate
    pip install -r inspection_api/requirements.txt
    export INSPECTION_DATABASE_URL='postgresql+psycopg://inspection_app:password@127.0.0.1:5432/inspection_db'
    uvicorn inspection_api.app:app --host 0.0.0.0 --port 8010

If INSPECTION_DATABASE_URL is absent or PostgreSQL is unavailable, inspection endpoints return HTTP 503 with a clear error. GET /health remains available.

## Browser CORS

`INSPECTION_CORS_ORIGINS` is a comma-separated allowlist of frontend origins. If it is unset, local Vite development origins `http://localhost:5173` and `http://127.0.0.1:5173` are allowed. For a frontend opened from another computer through the Ubuntu host IP, include that exact origin:

    export INSPECTION_CORS_ORIGINS='http://localhost:5173,http://127.0.0.1:5173,http://<ubuntu-ip>:5173'

Credentials are not enabled; wildcard origins are not used.

## Endpoints

- GET /health
- POST /api/v1/inspections
- GET /api/v1/inspections/{inspection_id}
- GET /api/v1/inspections/{inspection_id}/artifacts/{artifact_name}
- GET /api/v1/inspections?result=GOOD|DEFECT&device_id=jetson_nano_01&limit=100&offset=0

POST accepts multipart/form-data. Required field: metadata, containing a JSON string. Optional image fields are search_full, search_bbox, full, full_bbox, crop, heatmap, overlay, and anomalies. JPEG and PNG are accepted.

Example:

    curl -X POST http://127.0.0.1:8010/api/v1/inspections \
      -F "metadata=$(cat metadata.json)" \
      -F 'full=@01_full.jpg;type=image/jpeg'

Images stay on disk in data/inspections/<inspection_id>/. PostgreSQL stores validated metadata, anomaly grid/scores/regions, artifacts JSON, and storage_dir; it does not store image binaries.

Artifact files are served read-only only by their logical metadata name, for example `GET /api/v1/inspections/<inspection_id>/artifacts/crop`. The API resolves the server-side artifact mapping and rejects unknown names and paths outside that inspection directory.

## Tests

    cd /home/evgeny/CV/deepstream-car-tracking
    source inspection-api-venv/bin/activate
    python -m pytest inspection_api/tests -v

Unit tests use explicit SQLite injection only and do not exercise a real PostgreSQL server.
