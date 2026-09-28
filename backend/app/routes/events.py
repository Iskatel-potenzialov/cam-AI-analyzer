from datetime import datetime
from fastapi import APIRouter,Depends,Query,status,Response
from sqlalchemy import func,select
from sqlalchemy.orm import Session
from ..database import SessionLocal
from ..models import Event
from ..schemas import EventCreate,EventRead,EventSummary
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
def event_filters(camera_id:str|None,event_type:str|None,rule_id:str|None,object_class:str|None,direction:str|None,run_id:str|None,from_timestamp:datetime|None,to_timestamp:datetime):
 filters=[]
 for c,v in ((Event.camera_id,camera_id),(Event.event_type,event_type),(Event.rule_id,rule_id),(Event.object_class,object_class),(Event.direction,direction)):
  if v is not None:filters.append(c==v)
 if run_id is not None:filters.append(Event.attributes["run_id"].astext==run_id)
 if from_timestamp:filters.append(Event.timestamp>=from_timestamp)
 if to_timestamp:filters.append(Event.timestamp<=to_timestamp)
 return filters
@router.get("/summary",response_model=EventSummary)
def events_summary(camera_id:str|None=None,event_type:str|None=None,rule_id:str|None=None,object_class:str|None=None,direction:str|None=None,run_id:str|None=None,from_timestamp:datetime|None=None,to_timestamp:datetime|None=None,s:Session=Depends(db)):
 filters=event_filters(camera_id,event_type,rule_id,object_class,direction,run_id,from_timestamp,to_timestamp)
 row=s.execute(select(
  func.count(Event.event_id).label("total_events"),
  func.count(Event.event_id).filter(Event.rule_id=="red",Event.direction=="BOTTOM_TO_TOP").label("red_bottom_to_top"),
  func.count(Event.event_id).filter(Event.rule_id=="green",Event.direction=="LEFT_TO_RIGHT").label("green_left_to_right"),
  func.count(Event.event_id).filter(Event.rule_id=="green",Event.direction=="RIGHT_TO_LEFT").label("green_right_to_left"),
  func.count(Event.event_id).filter(Event.rule_id=="blue",Event.direction=="RIGHT_TO_LEFT").label("blue_right_to_left"),
 ).where(*filters)).one()
 return dict(row._mapping)
@router.get("",response_model=list[EventRead])
def list_events(camera_id:str|None=None,event_type:str|None=None,rule_id:str|None=None,object_class:str|None=None,direction:str|None=None,run_id:str|None=None,from_timestamp:datetime|None=None,to_timestamp:datetime|None=None,limit:int=Query(100,ge=1,le=1000),s:Session=Depends(db)):
 q=select(Event).where(*event_filters(camera_id,event_type,rule_id,object_class,direction,run_id,from_timestamp,to_timestamp))
 return s.scalars(q.order_by(Event.timestamp.desc(),Event.created_at.desc()).limit(limit)).all()
