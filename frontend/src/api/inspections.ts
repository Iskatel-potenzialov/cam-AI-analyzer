export type InspectionResult = "GOOD" | "DEFECT";

export interface InspectionRegion {
  rank: number;
  row: number;
  col: number;
  score: number;
  bbox_crop: number[];
  center_crop: number[];
}

export interface InspectionRecord {
  inspection_id: string;
  schema_version: string;
  device_id: string;
  timestamp: string;
  result: InspectionResult;
  detection: { class: string; confidence: number; bbox: number[] };
  anomaly: {
    model: string;
    embedding_dim: number;
    matching: string;
    threshold: number;
    ignore_border_patches: number;
    score_min: number;
    score_mean: number;
    score_p95: number;
    score_max: number;
    grid: { rows: number; cols: number; patch_count: number };
    scores: number[][];
    regions: InspectionRegion[];
  };
  quality: { sharpness: number; brightness: number; crop_width: number; crop_height: number };
  artifacts: Record<string, string>;
  storage_dir: string;
  created_at: string;
}

export interface InspectionFilters {
  result?: InspectionResult;
  deviceId?: string;
  limit?: number;
  offset?: number;
}

const base = (import.meta.env.VITE_INSPECTION_API_BASE_URL ?? "http://127.0.0.1:8010").replace(/\/$/, "");

export class InspectionApiError extends Error {
  constructor(public readonly status: number) {
    super(`Inspection API request failed: ${status}`);
  }
}

async function get<T>(path: string): Promise<T> {
  const response = await fetch(`${base}${path}`);
  if (!response.ok) {
    throw new InspectionApiError(response.status);
  }
  return response.json() as Promise<T>;
}

export function getInspections(filters: InspectionFilters = {}): Promise<InspectionRecord[]> {
  const params = new URLSearchParams();
  if (filters.result) params.set("result", filters.result);
  if (filters.deviceId?.trim()) params.set("device_id", filters.deviceId.trim());
  params.set("limit", String(filters.limit ?? 100));
  params.set("offset", String(filters.offset ?? 0));
  return get<InspectionRecord[]>(`/api/v1/inspections?${params}`);
}

export const getInspectionHealth = () => get<{ status: string }>("/health");

export const getInspection = (inspectionId: string) =>
  get<InspectionRecord>(`/api/v1/inspections/${encodeURIComponent(inspectionId)}`);

export const getInspectionArtifactUrl = (inspectionId: string, artifactName: string) =>
  `${base}/api/v1/inspections/${encodeURIComponent(inspectionId)}/artifacts/${encodeURIComponent(artifactName)}`;
