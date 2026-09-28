import type { Case2LiveState,EventFilters,EventRecord,EventSummary } from "../types";
const base=(import.meta.env.VITE_API_BASE_URL??"http://127.0.0.1:8000").replace(/\/$/,"");
export class ApiError extends Error {constructor(public readonly status:number){super(`API request failed: ${status}`);}}
function query(filters:EventFilters,limit?:number){const p=new URLSearchParams();for(const [k,v] of Object.entries(filters)){if(typeof v==="string"&&v.trim())p.set(k,v.trim());}if(limit)p.set("limit",String(limit));return p.size?`?${p}`:"";}
async function get<T>(path:string):Promise<T>{const r=await fetch(`${base}${path}`);if(!r.ok)throw new ApiError(r.status);return r.json() as Promise<T>;}
export const getHealth=()=>get<{status:string}>("/health");
export const source0SnapshotUrl=(cacheBuster:number)=>`${base}/api/v1/cameras/source0/snapshot.jpg?t=${cacheBuster}`;
export const source1SnapshotUrl=(cacheBuster:number)=>`${base}/api/v1/cameras/source1/snapshot.jpg?t=${cacheBuster}`;
export const getEvents=(filters:EventFilters,limit=100)=>get<EventRecord[]>(`/api/v1/events${query(filters,limit)}`);
export const getSummary=(filters:EventFilters)=>get<EventSummary>(`/api/v1/events/summary${query(filters)}`);
export async function getCase2LiveState():Promise<Case2LiveState|null>{try{return await get<Case2LiveState>("/api/v1/live-state/source1");}catch(error){if(error instanceof ApiError&&error.status===404)return null;throw error;}}