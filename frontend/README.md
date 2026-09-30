# Vehicle Analytics Dashboard - Stage 9A

A separate React/TypeScript SPA. It reads events only through FastAPI REST, never connects to PostgreSQL directly, and uses no WebSocket or SSE.

## Configure and run

```bash
cd /home/evgeny/CV/deepstream-car-tracking/frontend
cp .env.example .env
npm install
npm run dev
```

The default API URL is http://127.0.0.1:8000. Set VITE_API_BASE_URL in local .env to override it. Vite serves http://localhost:5173.

Start FastAPI separately:

```bash
cd /home/evgeny/CV
source backend-venv/bin/activate
cd /home/evgeny/CV/deepstream-car-tracking
set -a; source backend/.env; set +a
uvicorn app.main:app --reload --app-dir backend
```

The dashboard uses GET /health, GET /api/v1/events, and GET /api/v1/events/summary. TanStack Query polls every 3000 ms. Filters are camera_id, rule_id, direction, and producer run_id (attributes.run_id).

```bash
npm run build
npm run lint
```
## Domino inspection view

The “Инспекции домино” tab reads the isolated Inspection API. Configure its base URL in local `.env`:

```bash
VITE_INSPECTION_API_BASE_URL=http://127.0.0.1:8010
```

It uses `GET /api/v1/inspections` and `GET /api/v1/inspections/{inspection_id}`. The current Inspection API returns artifact paths only; it does not expose saved JPEG/PNG files through HTTP yet.
