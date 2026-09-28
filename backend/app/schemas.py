import math
import uuid
from datetime import datetime
from typing import Literal
from pydantic import BaseModel,ConfigDict,Field,field_validator,model_validator
class EventCreate(BaseModel):
 event_id:uuid.UUID
 schema_version:int=Field(default=1,ge=1)
 timestamp:datetime
 camera_id:str
 event_type:str
 rule_id:str|None=None;object_class:str|None=None;track_id:int|None=None;direction:str|None=None
 confidence:float|None=Field(default=None,ge=0,le=1)
 attributes:dict=Field(default_factory=dict)
 @field_validator("timestamp")
 @classmethod
 def aware(cls,v):
  if v.tzinfo is None or v.utcoffset() is None: raise ValueError("timestamp must be timezone-aware")
  return v
 @field_validator("camera_id","event_type")
 @classmethod
 def text(cls,v):
  v=v.strip()
  if not v: raise ValueError("must not be empty")
  return v
class EventRead(EventCreate):
 model_config=ConfigDict(from_attributes=True)
 created_at:datetime
class EventSummary(BaseModel):
 total_events:int
 red_bottom_to_top:int
 green_left_to_right:int
 green_right_to_left:int
 blue_right_to_left:int
class LiveCounts(BaseModel):
 red:int=Field(ge=0)
 yellow:int=Field(ge=0)
 green:int=Field(ge=0)
class LivePerson(BaseModel):
 track_id:int=Field(ge=0)
 zone:Literal["RED","YELLOW","GREEN"]
 confidence:float|None=None
 @field_validator("confidence")
 @classmethod
 def valid_confidence(cls,v):
  if v is not None and (not math.isfinite(v) or v<0 or v>1): raise ValueError("confidence must be finite and between 0 and 1")
  return v
class LiveStateCreate(BaseModel):
 model_config=ConfigDict(extra="forbid")
 camera_id:str
 counts:LiveCounts
 people:list[LivePerson]
 @field_validator("camera_id")
 @classmethod
 def live_camera_id(cls,v):
  v=v.strip()
  if not v: raise ValueError("must not be empty")
  return v
 @model_validator(mode="after")
 def valid_snapshot(self):
  track_ids=[person.track_id for person in self.people]
  if len(track_ids)!=len(set(track_ids)): raise ValueError("people must not contain duplicate track_id values")
  actual={zone:0 for zone in ("RED","YELLOW","GREEN")}
  for person in self.people: actual[person.zone]+=1
  if self.counts.red!=actual["RED"] or self.counts.yellow!=actual["YELLOW"] or self.counts.green!=actual["GREEN"]: raise ValueError("counts must match people zones")
  return self
class LiveStateRead(LiveStateCreate):
 updated_at:datetime