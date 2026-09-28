import {useEffect,useState,type FormEvent} from "react";
import {useQuery} from "@tanstack/react-query";
import {getEvents,getHealth,getSummary,source0SnapshotUrl} from "./api/events";
import Case2Page from "./Case2Page";
import type {EventFilters,EventRecord} from "./types";
const empty:EventFilters={camera_id:"",rule_id:"",direction:"",run_id:""};
const directions=["","BOTTOM_TO_TOP","LEFT_TO_RIGHT","RIGHT_TO_LEFT"];
const ruleNames:Record<string,string>={red:"красная",green:"зелёная",blue:"синяя"};
const directionNames:Record<string,string>={BOTTOM_TO_TOP:"снизу вверх",LEFT_TO_RIGHT:"слева направо",RIGHT_TO_LEFT:"справа налево"};
const classNames:Record<string,string>={car:"автомобиль",motorcycle:"мотоцикл",bus:"автобус"};
const stamp=(value:string)=>new Intl.DateTimeFormat(undefined,{dateStyle:"medium",timeStyle:"medium"}).format(new Date(value));
const confidence=(value:number|null)=>value===null?"-":value.toFixed(2);
const ruleName=(value:string|null)=>value===null?"-":ruleNames[value]??value;
const directionName=(value:string|null)=>value===null?"-":directionNames[value]??value;
const className=(value:string|null)=>value===null?"-":classNames[value]??value;
function Rule({event}:{event:EventRecord}){const rule=event.rule_id??"-";return <span className={`rule rule-${rule.toLowerCase()}`}>{ruleName(event.rule_id)}</span>;}
function Source0Snapshot({cards,loading}:{cards:[string,number,string][];loading:boolean}){
 const [url,setUrl]=useState(()=>source0SnapshotUrl(Date.now()));
 const [available,setAvailable]=useState<boolean|null>(null);
 useEffect(()=>{const refresh=()=>{setAvailable(null);setUrl(source0SnapshotUrl(Date.now()));};const timer=window.setInterval(refresh,1000);return()=>window.clearInterval(timer);},[]);
 return <section className="panel snapshot-panel"><div className="heading"><div><h2>Источник 0 — текущий кадр</h2><p>Рабочая ROI Case 1 с существующими bbox, линиями и идентификаторами.</p></div><small>Обновление каждую секунду</small></div><div className="case1-overview"><aside className="case1-summary"><h3>Состояние</h3><section className="cards case1-cards">{cards.map(([label,value,tone])=><article className={`card ${tone}`} key={label}><span>{label}</span><strong>{loading?"…":value}</strong></article>)}</section></aside><div className="snapshot-frame">{available!==false?<img src={url} alt="Текущий кадр source0" onLoad={()=>setAvailable(true)} onError={()=>setAvailable(false)}/>:<p className="message">Изображение камеры пока недоступно</p>}{available===null?<span className="snapshot-loading">Загрузка изображения…</span>:null}</div></div></section>;
}
function Case1Page(){
 const [draft,setDraft]=useState<EventFilters>(empty);const [filters,setFilters]=useState<EventFilters>(empty);
 const summary=useQuery({queryKey:["summary",filters],queryFn:()=>getSummary(filters),refetchInterval:3000,retry:1});
 const events=useQuery({queryKey:["events",filters],queryFn:()=>getEvents(filters),refetchInterval:3000,retry:1});
 const apply=(e:FormEvent)=>{e.preventDefault();setFilters(draft);};const reset=()=>{setDraft(empty);setFilters(empty);};
 const cards=[["ВСЕГО СОБЫТИЙ",summary.data?.total_events??0,"neutral"],["КРАСНАЯ: СНИЗУ ВВЕРХ",summary.data?.red_bottom_to_top??0,"red"],["ЗЕЛЁНАЯ: СЛЕВА НАПРАВО",summary.data?.green_left_to_right??0,"green"],["ЗЕЛЁНАЯ: СПРАВА НАЛЕВО",summary.data?.green_right_to_left??0,"green"],["СИНЯЯ: СПРАВА НАЛЕВО",summary.data?.blue_right_to_left??0,"blue"]];
 return <>
  <Source0Snapshot cards={cards} loading={summary.isLoading}/>
  <section className="panel"><div className="heading"><div><h2>Фильтры</h2><p>Статистика и последние события используют одинаковые фильтры.</p></div><small>Обновление каждые 3 секунды</small></div><form onSubmit={apply}><label>Камера<input value={draft.camera_id??""} onChange={e=>setDraft({...draft,camera_id:e.target.value})} placeholder="source0"/></label><label>Линия<select value={draft.rule_id??""} onChange={e=>setDraft({...draft,rule_id:e.target.value})}><option value="">Все линии</option><option value="red">красная</option><option value="green">зелёная</option><option value="blue">синяя</option></select></label><label>Направление<select value={draft.direction??""} onChange={e=>setDraft({...draft,direction:e.target.value})}>{directions.map(d=><option value={d} key={d}>{d?directionName(d):"Все направления"}</option>)}</select></label><label className="run">ID запуска<input value={draft.run_id??""} onChange={e=>setDraft({...draft,run_id:e.target.value})} placeholder="UUID из RUN_START"/></label><div className="actions"><button>Применить</button><button type="button" className="secondary" onClick={reset}>Сбросить</button></div></form></section>
  <section className="panel"><div className="heading"><div><h2>Последние события</h2><p>Сначала новые, максимум 100 записей.</p></div>{events.isFetching&&!events.isLoading?<small>Обновление...</small>:null}</div>{events.isLoading?<p className="message">Загрузка событий...</p>:null}{events.isError?<p className="message error">Не удалось загрузить события. Панель продолжит попытки автоматически.</p>:null}{!events.isLoading&&!events.isError&&events.data?.length===0?<p className="message">Нет событий, соответствующих выбранным фильтрам.</p>:null}{events.data&&events.data.length>0?<div className="table"><table><thead><tr><th>ВРЕМЯ</th><th>КАМЕРА</th><th>ЛИНИЯ</th><th>НАПРАВЛЕНИЕ</th><th>КЛАСС</th><th>TRACK ID</th><th>УВЕРЕННОСТЬ</th></tr></thead><tbody>{events.data.map(event=><tr key={event.event_id}><td>{stamp(event.timestamp)}</td><td>{event.camera_id}</td><td><Rule event={event}/></td><td>{directionName(event.direction)}</td><td>{className(event.object_class)}</td><td>{event.track_id??"-"}</td><td>{confidence(event.confidence)}</td></tr>)}</tbody></table></div>:null}</section>
 </>;
}
export default function App(){
 const [view,setView]=useState<"case1"|"case2">("case1");
 const health=useQuery({queryKey:["health"],queryFn:getHealth,refetchInterval:3000,retry:1});
 const online=health.data?.status==="ok";const unavailable=health.isError||(!health.isLoading&&!online);
 return <main className="dashboard"><header><div><p className="eyebrow">ПАНЕЛЬ СОБЫТИЙ DEEPSTREAM</p><h1>{view==="case1"?"Аналитика транспорта":"Контроль людей по зонам"}</h1></div><div className={`status ${unavailable?"offline":online?"online":""}`}><i/>{unavailable?"API недоступен":online?"API доступен":"Проверка API"}</div></header><nav className="case-nav" aria-label="Аналитические сценарии"><button type="button" className={view==="case1"?"active":""} onClick={()=>setView("case1")}>Case 1 — Аналитика транспорта</button><button type="button" className={view==="case2"?"active":""} onClick={()=>setView("case2")}>Case 2 — Контроль зон</button></nav>{view==="case1"?<Case1Page/>:<Case2Page/>}</main>;
}