import uuid
from datetime import datetime
from pydantic import BaseModel,ConfigDict,Field,field_validator
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