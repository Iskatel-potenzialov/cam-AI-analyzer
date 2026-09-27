import uuid
from datetime import datetime
from sqlalchemy import DateTime,Integer,String,Float,func
from sqlalchemy.dialects.postgresql import UUID,JSONB
from sqlalchemy.orm import Mapped,mapped_column
from .database import Base
class Event(Base):
 __tablename__="events"
 event_id:Mapped[uuid.UUID]=mapped_column(UUID(as_uuid=True),primary_key=True)
 schema_version:Mapped[int]=mapped_column(Integer,nullable=False,default=1)
 timestamp:Mapped[datetime]=mapped_column(DateTime(timezone=True),nullable=False,index=True)
 camera_id:Mapped[str]=mapped_column(String,nullable=False,index=True)
 event_type:Mapped[str]=mapped_column(String,nullable=False,index=True)
 rule_id:Mapped[str|None]=mapped_column(String,index=True)
 object_class:Mapped[str|None]=mapped_column(String)
 track_id:Mapped[int|None]=mapped_column(Integer)
 direction:Mapped[str|None]=mapped_column(String)
 confidence:Mapped[float|None]=mapped_column(Float)
 attributes:Mapped[dict]=mapped_column(JSONB,nullable=False,default=dict)
 created_at:Mapped[datetime]=mapped_column(DateTime(timezone=True),server_default=func.now(),nullable=False)