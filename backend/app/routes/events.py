from datetime import datetime
from fastapi import APIRouter,Depends,Query,status,Response
from sqlalchemy import select
from sqlalchemy.orm import Session
from ..database import SessionLocal
from ..models import Event
from ..schemas import EventCreate,EventRead
router=APIRouter(prefix="/api/v1/events",tags=["events"])
def db():
 s=SessionLocal()
 try: yield s
 finally: s.close()
@router.post("",response_model=EventRead,status_code=status.HTTP_201_CREATED)
def create(event:EventCreate,response:Response,s:Session=Depends(db)):
 existing=s.get(Event,event.event_id)
 if existing:
  response.status_code=status.HTTP_200_OK
  return existing
 row=Event(**event.model_dump());s.add(row);s.commit();s.refresh(row);return row
@router.get("",response_model=list[EventRead])
def list_events(camera_id:str|None=None,event_type:str|None=None,rule_id:str|None=None,object_class:str|None=None,from_timestamp:datetime|None=None,to_timestamp:datetime|None=None,limit:int=Query(100,ge=1,le=1000),s:Session=Depends(db)):
 q=select(Event)
 for c,v in ((Event.camera_id,camera_id),(Event.event_type,event_type),(Event.rule_id,rule_id),(Event.object_class,object_class)):
  if v is not None:q=q.where(c==v)
 if from_timestamp:q=q.where(Event.timestamp>=from_timestamp)
 if to_timestamp:q=q.where(Event.timestamp<=to_timestamp)
 return s.scalars(q.order_by(Event.timestamp.desc(),Event.created_at.desc()).limit(limit)).all()