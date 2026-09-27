# Backend — Stage 8B

Independent FastAPI/PostgreSQL event API; it does not use `CV-venv` or DeepStream.

## Event

`event_id` UUID producer idempotency key; `schema_version` >=1; timezone-aware `timestamp`; non-empty `camera_id` and `event_type`; optional rule/object/track/direction/confidence; JSON-object `attributes`; server `created_at`.

```json
{"event_id":"550e8400-e29b-41d4-a716-446655440000","schema_version":1,"timestamp":"2026-09-27T09:15:32+05:00","camera_id":"cam_01","event_type":"line_crossing","rule_id":"green_line","object_class":"car","track_id":497,"direction":"right_to_left","confidence":0.87,"attributes":{}}
```

Endpoints: `GET /health`; `POST /api/v1/events` returns 201 for new and 200 for duplicate `event_id`; `GET /api/v1/events` filters camera_id,event_type,rule_id,object_class,from_timestamp,to_timestamp, with limit 1..1000 (default 100), newest first.

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
