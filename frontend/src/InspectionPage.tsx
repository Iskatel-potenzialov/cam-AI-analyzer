import { useState } from "react";
import { useQuery } from "@tanstack/react-query";
import {
  getInspection,
  getInspectionArtifactUrl,
  getInspections,
  type InspectionRecord,
  type InspectionResult,
} from "./api/inspections";

const artifactSlots = [
  ["crop", "Crop"],
  ["anomalies", "Top anomalies"],
  ["heatmap", "Anomaly heatmap"],
  ["overlay", "Anomaly overlay"],
  ["full_bbox", "Full frame + bbox"],
  ["full", "Full frame"],
] as const;

const formatTime = (value: string) =>
  new Intl.DateTimeFormat(undefined, { dateStyle: "medium", timeStyle: "medium" }).format(new Date(value));
const number = (value: number) => value.toFixed(3);
const bbox = (value: number[]) => `[${value.map((item) => item.toFixed(1)).join(", ")}]`;

function ResultBadge({ result }: { result: InspectionResult }) {
  return <span className={`inspection-result ${result.toLowerCase()}`}>{result}</span>;
}

function InspectionDetails({ inspection }: { inspection: InspectionRecord }) {
  const { anomaly, detection, quality } = inspection;
  return <section className="panel inspection-details">
    <div className="heading">
      <div>
        <h2>Инспекция</h2>
        <p>{inspection.inspection_id}</p>
      </div>
      <ResultBadge result={inspection.result} />
    </div>

    <div className="inspection-facts">
      <article><span>Время</span><strong>{formatTime(inspection.timestamp)}</strong></article>
      <article><span>Устройство</span><strong>{inspection.device_id}</strong></article>
      <article><span>Класс</span><strong>{detection.class}</strong></article>
      <article><span>Уверенность</span><strong>{number(detection.confidence)}</strong></article>
      <article className="wide"><span>BBox</span><strong>{bbox(detection.bbox)}</strong></article>
    </div>

    <div className="inspection-columns">
      <section>
        <h3>Аномалия</h3>
        <dl className="inspection-metrics">
          <div><dt>Модель</dt><dd>{anomaly.model}</dd></div>
          <div><dt>Порог</dt><dd>{number(anomaly.threshold)}</dd></div>
          <div><dt>Минимум</dt><dd>{number(anomaly.score_min)}</dd></div>
          <div><dt>Среднее</dt><dd>{number(anomaly.score_mean)}</dd></div>
          <div><dt>P95</dt><dd>{number(anomaly.score_p95)}</dd></div>
          <div><dt>Максимум</dt><dd>{number(anomaly.score_max)}</dd></div>
        </dl>
      </section>
      <section>
        <h3>Качество кадра</h3>
        <dl className="inspection-metrics">
          <div><dt>Резкость</dt><dd>{number(quality.sharpness)}</dd></div>
          <div><dt>Яркость</dt><dd>{number(quality.brightness)}</dd></div>
          <div><dt>Crop</dt><dd>{quality.crop_width} × {quality.crop_height}</dd></div>
          <div><dt>Сетка</dt><dd>{anomaly.grid.rows} × {anomaly.grid.cols}</dd></div>
        </dl>
      </section>
    </div>

    <section className="inspection-subsection">
      <h3>Регионы аномалий</h3>
      {anomaly.regions.length === 0 ? <p className="message compact">Аномальные регионы не переданы.</p> : <div className="table"><table>
        <thead><tr><th>Ранг</th><th>Патч</th><th>Score</th><th>BBox crop</th></tr></thead>
        <tbody>{anomaly.regions.map((region) => <tr key={`${region.rank}-${region.row}-${region.col}`}>
          <td>{region.rank}</td><td>{region.row}, {region.col}</td><td>{number(region.score)}</td><td>{bbox(region.bbox_crop)}</td>
        </tr>)}</tbody>
      </table></div>}
    </section>

    <section className="inspection-subsection">
      <h3>Images</h3>
      {artifactSlots.some(([key]) => inspection.artifacts[key]) ? <div className="inspection-artifact-images">
        {artifactSlots.map(([key, label]) => inspection.artifacts[key] ? <figure key={key}>
          <figcaption>{label}</figcaption>
          <img src={getInspectionArtifactUrl(inspection.inspection_id, key)} alt={label} />
        </figure> : null)}
      </div> : <p className="artifact-note">????? ??????????? ?? ????????.</p>}
    </section>
  </section>;
}

export default function InspectionPage() {
  const [draftResult, setDraftResult] = useState<"ALL" | InspectionResult>("ALL");
  const [draftDevice, setDraftDevice] = useState("");
  const [result, setResult] = useState<"ALL" | InspectionResult>("ALL");
  const [deviceId, setDeviceId] = useState("");
  const [selectedId, setSelectedId] = useState<string | null>(null);

  const list = useQuery({
    queryKey: ["inspections", result, deviceId],
    queryFn: () => getInspections({ result: result === "ALL" ? undefined : result, deviceId }),
    retry: 1,
  });
  const detail = useQuery({
    queryKey: ["inspection", selectedId],
    queryFn: () => getInspection(selectedId!),
    enabled: selectedId !== null,
    retry: 1,
  });

  const apply = () => {
    setResult(draftResult);
    setDeviceId(draftDevice);
    setSelectedId(null);
  };
  const refresh = () => {
    void list.refetch();
    if (selectedId) void detail.refetch();
  };

  return <>
    <section className="panel inspection-list-panel">
      <div className="heading">
        <div><h2>Результаты инспекций</h2><p>Новые результаты отображаются сверху в порядке API.</p></div>
        <button type="button" className="secondary" onClick={refresh} disabled={list.isFetching}>Обновить</button>
      </div>
      <div className="inspection-filters">
        <label>Результат<select value={draftResult} onChange={(event) => setDraftResult(event.target.value as "ALL" | InspectionResult)}>
          <option value="ALL">Все</option><option value="GOOD">GOOD</option><option value="DEFECT">DEFECT</option>
        </select></label>
        <label>Устройство<input value={draftDevice} onChange={(event) => setDraftDevice(event.target.value)} placeholder="jetson_nano_01" /></label>
        <button type="button" onClick={apply}>Применить</button>
      </div>
      {list.isLoading ? <p className="message">Загрузка инспекций…</p> : null}
      {list.isError ? <p className="message error">Не удалось загрузить инспекции. Проверьте доступность Inspection API.</p> : null}
      {!list.isLoading && !list.isError && list.data?.length === 0 ? <p className="message">Инспекций с выбранными фильтрами нет.</p> : null}
      {list.data?.length ? <div className="table inspection-table"><table>
        <thead><tr><th>Время</th><th>ID инспекции</th><th>Устройство</th><th>Результат</th><th>Confidence</th><th>Score max</th><th>Score mean</th></tr></thead>
        <tbody>{list.data.map((inspection) => <tr key={inspection.inspection_id} className={selectedId === inspection.inspection_id ? "selected" : ""} onClick={() => setSelectedId(inspection.inspection_id)}>
          <td>{formatTime(inspection.timestamp)}</td><td>{inspection.inspection_id}</td><td>{inspection.device_id}</td><td><ResultBadge result={inspection.result} /></td><td>{number(inspection.detection.confidence)}</td><td>{number(inspection.anomaly.score_max)}</td><td>{number(inspection.anomaly.score_mean)}</td>
        </tr>)}</tbody>
      </table></div> : null}
    </section>
    {detail.isLoading ? <section className="panel"><p className="message">Загрузка деталей инспекции…</p></section> : null}
    {detail.isError ? <section className="panel"><p className="message error">Не удалось загрузить детали выбранной инспекции.</p></section> : null}
    {detail.data ? <InspectionDetails inspection={detail.data} /> : null}
  </>;
}