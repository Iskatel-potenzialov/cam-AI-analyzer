import {useEffect,useState} from "react";
import {useQuery} from "@tanstack/react-query";
import {getCase2LiveState,getEvents,source1SnapshotUrl} from "./api/events";
import type {Case2LiveState,Case2Person,Case2Zone,EventRecord} from "./types";

const historyFilters={camera_id:"source1",event_type:"red_zone_entry",rule_id:"case2_red"};
const zoneNames:Record<Case2Zone,string>={RED:"КРАСНАЯ",YELLOW:"ЖЁЛТАЯ",GREEN:"ЗЕЛЁНАЯ"};
const stamp=(value:string)=>new Intl.DateTimeFormat(undefined,{dateStyle:"medium",timeStyle:"medium"}).format(new Date(value));
const confidence=(value:number|null)=>value===null?"—":value.toFixed(2);

function Source1Snapshot({snapshot,loading,error}:{snapshot:Case2LiveState|null|undefined;loading:boolean;error:boolean}){
 const [url,setUrl]=useState(()=>source1SnapshotUrl(Date.now()));
 const [available,setAvailable]=useState<boolean|null>(null);
 useEffect(()=>{const refresh=()=>{setAvailable(null);setUrl(source1SnapshotUrl(Date.now()));};const timer=window.setInterval(refresh,1000);return()=>window.clearInterval(timer);},[]);
 return <section className="panel snapshot-panel"><div className="heading"><div><h2>Источник 1 — текущий кадр</h2><p>Отображение с существующими bbox, зонами и идентификаторами.</p></div><small>Обновление каждую секунду</small></div><div className="case2-overview"><aside className="zone-summary"><h3>Состояние зон</h3><ZoneCounters snapshot={snapshot} loading={loading}/></aside><div className="snapshot-frame">{available!==false?<img src={url} alt="Текущий кадр source1" onLoad={()=>setAvailable(true)} onError={()=>setAvailable(false)}/>:<p className="message">Изображение камеры пока недоступно</p>}{available===null?<span className="snapshot-loading">Загрузка изображения…</span>:null}</div></div>{error?<p className="message error">Не удалось получить текущее состояние зон. {snapshot?"Показан последний полученный snapshot.":""}</p>:null}{!loading&&!error&&snapshot===null?<p className="message">Snapshot source1 пока не получен.</p>:null}</section>;
}function ZoneCounters({snapshot,loading}:{snapshot:Case2LiveState|null|undefined;loading:boolean}){const cards:[string,number|undefined,string][]=[["КРАСНАЯ",snapshot?.counts.red,"red"],["ЖЁЛТАЯ",snapshot?.counts.yellow,"yellow"],["ЗЕЛЁНАЯ",snapshot?.counts.green,"green"]];return <section className="cards case2-cards">{cards.map(([label,value,tone])=><article className={`card ${tone}`} key={label}><span>{label}</span><strong>{loading?"…":value??"—"}</strong></article>)}</section>;}
function Zone({zone}:{zone:Case2Zone}){return <span className={`zone zone-${zone.toLowerCase()}`}>{zoneNames[zone]}</span>;}
function ActivePeopleTable({people}:{people:Case2Person[]}){if(!people.length)return <p className="message">Сейчас в контролируемых зонах нет людей.</p>;return <div className="table"><table><thead><tr><th>TRACK ID</th><th>ЗОНА</th><th>УВЕРЕННОСТЬ</th></tr></thead><tbody>{people.map(person=><tr key={person.track_id}><td>{person.track_id}</td><td><Zone zone={person.zone}/></td><td>{confidence(person.confidence)}</td></tr>)}</tbody></table></div>;}
function RedEntryHistory({events,loading,error}:{events:EventRecord[]|undefined;loading:boolean;error:boolean}){if(loading)return <p className="message">Загрузка истории…</p>;if(error)return <p className="message error">Не удалось загрузить историю входов в красную зону.</p>;if(!events?.length)return <p className="message">Входов в красную зону пока нет.</p>;return <div className="table"><table><thead><tr><th>ВРЕМЯ</th><th>TRACK ID</th><th>УВЕРЕННОСТЬ</th></tr></thead><tbody>{events.map(event=><tr key={event.event_id}><td>{stamp(event.timestamp)}</td><td>{event.track_id??"—"}</td><td>{confidence(event.confidence)}</td></tr>)}</tbody></table></div>;}

export default function Case2Page(){
 const live=useQuery({queryKey:["case2-live-state","source1"],queryFn:getCase2LiveState,refetchInterval:1000,retry:1});
 const history=useQuery({queryKey:["case2-red-entry-history"],queryFn:()=>getEvents(historyFilters,100),refetchInterval:10000,retry:1});
 const snapshot=live.data;
 return <>
  <Source1Snapshot snapshot={snapshot} loading={live.isLoading} error={live.isError}/>
  <section className="panel"><div className="heading"><div><h2>Люди в контролируемых зонах</h2><p>Tracker ID — технический идентификатор текущего NvDCF track.</p></div>{snapshot?<small>Последнее обновление: {stamp(snapshot.updated_at)}</small>:null}</div>{snapshot?<ActivePeopleTable people={snapshot.people}/>:null}</section>
  <section className="panel"><div className="heading"><div><h2>История входов в красную зону</h2><p>События source1 с правилом case2_red.</p></div>{history.isFetching&&!history.isLoading?<small>Обновление…</small>:null}</div><RedEntryHistory events={history.data} loading={history.isLoading} error={history.isError}/></section>
 </>;
}