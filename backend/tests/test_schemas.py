import pytest
from datetime import datetime,timezone
from pydantic import ValidationError
from app.schemas import EventCreate
P={"event_id":"550e8400-e29b-41d4-a716-446655440000","timestamp":"2026-09-27T09:15:32+05:00","camera_id":"cam_01","event_type":"line_crossing","attributes":{}}
def test_valid(): assert EventCreate(**P).camera_id=="cam_01"
@pytest.mark.parametrize("key,value",[("confidence",-0.1),("confidence",1.1),("timestamp","2026-09-27T09:15:32"),("camera_id"," "),("event_type"," "),("schema_version",0),("attributes",[])])
def test_invalid(key,value):
 p=P|{key:value}
 with pytest.raises(ValidationError):EventCreate(**p)