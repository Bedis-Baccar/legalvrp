// Plans executed with the true service times: planned vs realised timelines, late and illegal duties.
import type { Bundle, Route, ScenarioEntry } from "../types";
import { driverName, Loading, routeRows } from "../util";
import { DAYS, dur, eur, pct, ruleTitle } from "../format";
import { RUN_COLORS } from "./Charts";
import Gantt, { Legend } from "./Gantt";
import { useCompareRuns } from "./WeekView";
import { Stat } from "./DayView";

function tally(b: Bundle) {
  const duties: Route[] = b.days.flatMap((d) => d.realised?.routes ?? []);
  const late = duties.filter((r) => r.outcome?.late).length;
  const illegal = duties.filter((r) => r.outcome?.illegal).length;
  const either = duties.filter((r) => r.outcome?.late || r.outcome?.illegal).length;
  const stops = duties.reduce((s, r) => s + r.order_ids.length, 0);
  const lateStops = duties.reduce((s, r) => s + (r.outcome?.late_stops ?? 0), 0);
  return { duties: duties.length, late, illegal, either, stops, lateStops };
}

export default function RealView({ sc, run, day, onChange }: { sc: ScenarioEntry; run: string; day: number; onChange: (run: string, day: number) => void }) {
  const { runs, bundles, error } = useCompareRuns(sc);
  if (!bundles) return <Loading error={error} />;
  const idx = Math.max(0, runs.findIndex((r) => r.id === run));
  const b = bundles[idx];
  const ref = bundles[0];
  const d = b?.days.find((x) => x.day === day) ?? b?.days[0];
  return (
    <div className="stack">
      <section className="card">
        <h3>Sur toute la semaine, avec les vrais temps de livraison</h3>
        <div className="realcards">
          {runs.map((r, i) => {
            const bb = bundles[i];
            if (!bb) return null;
            if (!bb.days[0]?.realised) return <div key={r.id} className="notice">{r.label} : pas de temps réels pour cette instance.</div>;
            const t = tally(bb);
            return (
              <button key={r.id} className={`realcard${i === idx ? " on" : ""}`} onClick={() => onChange(r.id, d?.day ?? 0)}>
                <div className="realcard-title">
                  <span className="dot" style={{ background: RUN_COLORS[i % RUN_COLORS.length] }} /> {r.label}
                </div>
                <div className="bigpct">
                  <span className={t.either ? "bad" : "good"}>{t.either}</span> / {t.duties}
                </div>
                <div className="small">tournées en retard ou illégales</div>
                <div className="small muted">
                  {t.late} en retard ({t.lateStops} livraisons sur {t.stops}) · {t.illegal} illégales
                </div>
                <div className="small">
                  Coût planifié {eur(bb.kpis.cost_total)}
                  {i > 0 && ref ? <span className="muted"> ({pct((bb.kpis.cost_total - ref.kpis.cost_total) / ref.kpis.cost_total)})</span> : null}
                </div>
              </button>
            );
          })}
        </div>
      </section>
      {b && d && d.realised && (
        <>
          <div className="toolbar">
            <div className="seg-control">
              {b.days.map((x) => {
                const bad = (x.realised?.routes ?? []).filter((r) => r.outcome?.late || r.outcome?.illegal).length;
                return (
                  <button key={x.day} className={x.day === d.day ? "on" : ""} onClick={() => onChange(runs[idx].id, x.day)}>
                    {DAYS[x.day]} {bad ? <span className="pill bad">{bad}</span> : <span className="pill ok">✓</span>}
                  </button>
                );
              })}
            </div>
          </div>
          <div className="stats">
            <Stat label="Tournées" value={`${d.realised.routes.length}`} />
            <Stat label="En retard" value={`${d.realised.routes.filter((r) => r.outcome?.late).length}`} warn={d.realised.routes.some((r) => r.outcome?.late)} />
            <Stat label="Illégales" value={`${d.realised.routes.filter((r) => r.outcome?.illegal).length}`} bad={d.realised.routes.some((r) => r.outcome?.illegal)} />
            <Stat label="Plus gros retard" value={dur(Math.max(0, ...d.realised.routes.map((r) => r.outcome?.max_delay ?? 0)))} />
            <Stat label="Retour le plus tardif" value={`+${dur(Math.max(0, ...d.realised.routes.map((r) => r.outcome?.overrun ?? 0)))}`} sub="par rapport au plan" />
          </div>
          <section className="card">
            <h3>Prévu — {DAYS[d.day]}</h3>
            <Legend />
            <Gantt rows={routeRows(b, d, d.routes)} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} />
          </section>
          <section className="card">
            <h3>Réalisé — mêmes départs, même ordre, mêmes pauses, vrais temps de livraison</h3>
            <Gantt rows={routeRows(b, d, d.realised.routes, d.routes)} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} />
            <p className="muted small">Fin trait gris au-dessus de chaque barre : le plan prévu. Segments rouges : compteur de la loi dépassé ; livraisons rouges : créneau manqué.</p>
            <ul className="plain small">
              {d.realised.routes
                .filter((r) => r.outcome?.illegal)
                .map((r) => (
                  <li key={r.driver_id}>
                    <b>{driverName(b, r.driver_id)}</b> : {r.outcome!.rules.map(ruleTitle).join(", ")}
                  </li>
                ))}
            </ul>
          </section>
        </>
      )}
    </div>
  );
}
