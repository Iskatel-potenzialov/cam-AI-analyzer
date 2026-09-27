from fastapi.testclient import TestClient
from app.main import app
c=TestClient(app)
def test_health(): assert c.get("/health").json()=={"status":"ok"}
def test_limit_invalid(): assert c.get("/api/v1/events?limit=1001").status_code==422