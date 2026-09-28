export interface EventRecord {event_id:string;schema_version:number;timestamp:string;camera_id:string;event_type:string;rule_id:string|null;object_class:string|null;track_id:number|null;direction:string|null;confidence:number|null;attributes:Record<string,unknown>;created_at:string;}
export interface EventSummary {total_events:number;red_bottom_to_top:number;green_left_to_right:number;green_right_to_left:number;blue_right_to_left:number;}
export interface EventFilters {camera_id?:string;event_type?:string;rule_id?:string;object_class?:string;direction?:string;run_id?:string;from_timestamp?:string;to_timestamp?:string;}
export type Case2Zone="RED"|"YELLOW"|"GREEN";
export interface Case2Person {track_id:number;zone:Case2Zone;confidence:number|null;}
export interface Case2LiveState {camera_id:string;counts:{red:number;yellow:number;green:number;};people:Case2Person[];updated_at:string;}