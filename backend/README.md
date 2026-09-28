# Backend - Stage 8B

Independent FastAPI/PostgreSQL event API; it does not use `CV-venv` or DeepStream.

## Event

`event_id` UUID producer idempotency key; `schema_version` >=1; timezone-aware `timestamp`; non-empty `camera_id` and `event_type`; optional rule/object/track/direction/confidence; JSON-object `attributes`; server `created_at`.

```json
{"event_id":"550e8400-e29b-41d4-a716-446655440000","schema_version":1,"timestamp":"2026-09-27T09:15:32+05:00","camera_id":"cam_01","event_type":"line_crossing","rule_id":"green","object_class":"car","track_id":497,"direction":"RIGHT_TO_LEFT","confidence":0.87,"attributes":{"run_id":"550e8400-e29b-41d4-a716-446655440001"}}
```

Endpoints: `GET /health`; `POST /api/v1/events` returns 201 for new and 200 for duplicate `event_id`; `GET /api/v1/events` filters camera_id,event_type,rule_id,object_class,direction,run_id,from_timestamp,to_timestamp, with limit 1..1000 (default 100), newest first. `GET /api/v1/events/summary` accepts the same filters and returns server-side totals for the four Stage 7 directions. `run_id` matches `attributes.run_id`.

`DATABASE_URL` is required only from local environment, e.g. `postgresql+psycopg://...`; never commit `.env`.

```bash
cd /home/evgeny/CV
python3.10 -m venv backend-venv
source backend-venv/bin/activate
cd /home/evgeny/CV/deepstream-car-tracking
python -m pip install -r backend/requirements.txt
cp backend/.env.example backend/.env
set -a; source backend/.env; set +a
cd backend
alembic upgrade head
uvicorn app.main:app --reload
pytest tests/test_schemas.py tests/test_api_unit.py
pytest -m integration tests/test_events_postgres.py
```

Integration tests require an isolated migrated PostgreSQL database; SQLite is not used.

DATABASE_URL is only app/dev DB. TEST_DATABASE_URL must point to separately migrated cv_events_test; integration tests skip if it is absent and reject URLs not containing cv_events_test. Run test migration with DATABASE_URL=$TEST_DATABASE_URL alembic upgrade head, then TEST_DATABASE_URL=... pytest -m integration tests/test_events_postgres.py.
## Live state (Case 2C.1)

`POST /api/v1/live-state` accepts a complete current snapshot for one camera and replaces that camera's previous snapshot in backend process RAM. The backend assigns timezone-aware UTC `updated_at`. `GET /api/v1/live-state/{camera_id}` returns that latest snapshot or `404` when no snapshot has been posted.

A snapshot contains non-negative `counts.red`, `counts.yellow`, and `counts.green`, plus `people` entries with unique non-negative `track_id`, zone `RED`, `YELLOW`, or `GREEN`, and optional finite confidence from 0 to 1. Counts must exactly match the corresponding people zones. This is live UI state, not event history: it is not written to PostgreSQL and is lost on backend restart.
