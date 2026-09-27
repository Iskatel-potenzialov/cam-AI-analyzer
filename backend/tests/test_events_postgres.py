import os,uuid,pytest
from datetime import datetime,timezone,timedelta
pytestmark=pytest.mark.integration
URL=os.getenv("TEST_DATABASE_URL")
if not URL: pytest.skip("TEST_DATABASE_URL is required; integration tests never use DATABASE_URL",allow_module_level=True)
if "cv_events_test" not in URL: pytest.fail("TEST_DATABASE_URL must target cv_events_test")
os.environ["DATABASE_URL"]=URL
from fastapi.testclient import TestClient
from sqlalchemy import create_engine,text
from app.main import app
client=TestClient(app);engine=create_engine(URL)
@pytest.fixture(autouse=True)
def clean():
 with engine.begin() as c:c.execute(text("TRUNCATE events"))
def event(**x):
 d={"event_id":str(uuid.uuid4()),"timestamp":"2026-09-27T09:15:32+00:00","camera_id":"cam_01","event_type":"line_crossing","rule_id":"green","object_class":"car","attributes":{}};d.update(x);return d
def test_post_and_idempotency():
 e=event();assert client.post("/api/v1/events",json=e).status_code==201;r=client.post("/api/v1/events",json=e);assert r.status_code==200
 with engine.connect() as c:assert c.execute(text("select count(*) from events where event_id=:id"),{"id":e["event_id"]}).scalar()==1
def test_filters_order_limit():
 a=event(timestamp="2026-09-27T09:00:00+00:00");b=event(camera_id="cam_02",event_type="zone_enter",rule_id="blue",object_class="bus",timestamp="2026-09-27T10:00:00+00:00")
 for e in(a,b):assert client.post("/api/v1/events",json=e).status_code==201
 assert client.get("/api/v1/events",params={"camera_id":"cam_02"}).json()[0]["event_id"]==b["event_id"]
 assert client.get("/api/v1/events",params={"event_type":"zone_enter"}).json()[0]["event_id"]==b["event_id"]
 assert client.get("/api/v1/events",params={"rule_id":"blue"}).json()[0]["event_id"]==b["event_id"]
 assert client.get("/api/v1/events",params={"object_class":"bus"}).json()[0]["event_id"]==b["event_id"]
 assert client.get("/api/v1/events",params={"from_timestamp":"2026-09-27T09:30:00+00:00"}).json()[0]["event_id"]==b["event_id"]
 assert client.get("/api/v1/events",params={"to_timestamp":"2026-09-27T09:30:00+00:00"}).json()[0]["event_id"]==a["event_id"]
 assert client.get("/api/v1/events",params={"limit":1}).json()[0]["event_id"]==b["event_id"]
