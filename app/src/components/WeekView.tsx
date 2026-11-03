// The week: KPIs of every run side by side, cost per day.
import type { Bundle, RunEntry, ScenarioEntry } from "../types";
import { useBundles } from "../data";
import { DAYS, eur, km, num, pct } from "../format";
import { Loading } from "../util";
import { GroupedBars, RUN_COLORS, SeriesLegend } from "./Charts";

interface Metric {
  label: string;
  value: (b: Bundle) => number;
  fmt: (v: number) => string;
  better: "low" | "high";
  hint?: string;
}

const METRICS: Metric[] = [
  { label: "Coût de la semaine", value: (b) => b.kpis.cost_total, fmt: eur, better: "low", hint: "km + heures + heures sup. + intérim + pénalités de report (coûts illustratifs)" },
  { label: "Kilomètres", value: (b) => b.kpis.km, fmt: km, better: "low" },
  { label: "Commandes livrées", value: (b) => b.kpis.served, fmt: (v) => num(v, 0), better: "high" },
  { label: "Reports (décisions)", value: (b) => b.kpis.postponement_decisions, fmt: (v) => num(v, 0), better: "low", hint: "une commande reportée deux fois compte deux fois" },
  { label: "Non livrées vendredi soir", value: (b) => b.kpis.unserved_end, fmt: (v) => num(v, 0), better: "low" },
  { label: "Heures sup. / complémentaires", value: (b) => b.kpis.extra_minutes_total / 60, fmt: (v) => `${num(v)} h`, better: "low" },
  { label: "Tournées (chauffeur × jour)", value: (b) => b.days.reduce((s, d) => s + d.routes.length, 0), fmt: (v) => num(v, 0), better: "low" },
  { label: "Équité des heures (Gini, temps plein)", value: (b) => b.kpis.hours_gini, fmt: (v) => num(v, 3), better: "low", hint: "0 = heures identiques" },
];

export function useCompareRuns(sc: ScenarioEntry): { runs: RunEntry[]; bundles?: (Bundle | undefined)[]; error?: string } {
  const ids = sc.compare.length ? sc.compare : sc.runs.map((r) => r.id);
  const runs = ids.map((id) => sc.runs.find((r) => r.id === id)).filter((r): r is RunEntry => !!r);
  const { data, error } = useBundles(runs.map((r) => r.file));
  return { runs, bundles: data, error };
}

export default function WeekView({ sc, onOpenDay }: { sc: ScenarioEntry; onOpenDay: (run: string, day: number) => void }) {
  const { runs, bundles, error } = useCompareRuns(sc);
  if (!bundles) return <Loading error={error} />;
  const avail = runs.map((r, i) => ({ r, b: bundles[i] })).filter((x): x is { r: RunEntry; b: Bundle } => !!x.b);
  const ref = avail[0]?.b;
  const days = ref ? ref.days.map((d) => DAYS[d.day]) : [];
  return (
    <div className="stack">
      <section className="card">
        <h3>Indicateurs de la semaine</h3>
        <div className="table-wrap">
          <table className="kpi">
            <thead>
              <tr>
                <th />
                {runs.map((r, i) => (
                  <th key={r.id}>
                    <span className="dot" style={{ background: RUN_COLORS[i % RUN_COLORS.length] }} />
                    {r.label}
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              <tr>
                <td>Vérificateur légal</td>
                {runs.map((r, i) => {
                  const b = bundles[i];
                  if (!b) return <td key={r.id} className="muted">{r.unavailable ?? "—"}</td>;
                  return (
                    <td key={r.id}>
                      {b.week_check.ok ? <span className="badge ok">0 infraction</span> : <span className="badge bad">{b.week_check.violations.length} infraction(s)</span>}
                    </td>
                  );
                })}
              </tr>
              {METRICS.map((m) => (
                <tr key={m.label}>
                  <td title={m.hint}>
                    {m.label}
                    {m.hint && <span className="hint"> ⓘ</span>}
                  </td>
                  {runs.map((r, i) => {
                    const b = bundles[i];
                    if (!b) return <td key={r.id} className="muted">—</td>;
                    const v = m.value(b);
                    const v0 = ref ? m.value(ref) : v;
                    const rel = i > 0 && v0 !== 0 ? (v - v0) / Math.abs(v0) : 0;
                    const good = m.better === "low" ? v < v0 : v > v0;
                    return (
                      <td key={r.id}>
                        {m.fmt(v)}
                        {i > 0 && Math.abs(rel) > 0.0005 && m.label.startsWith("Coût") && <span className={good ? "delta good" : "delta bad"}>{pct(rel)}</span>}
                      </td>
                    );
                  })}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="muted small">La première colonne sert de référence. Tous les chiffres sont recalculés par le vérificateur indépendant à partir des tournées.</p>
      </section>

      {ref && (
        <section className="card">
          <h3>Coût par jour</h3>
          <SeriesLegend labels={avail.map((x) => x.r.label)} colors={avail.map((x) => RUN_COLORS[runs.indexOf(x.r) % RUN_COLORS.length])} />
          <GroupedBars groups={days} series={avail.map((x) => ({ label: x.r.label, values: x.b.kpis.days.map((d) => d.cost) }))} format={eur}
            colors={avail.map((x) => RUN_COLORS[runs.indexOf(x.r) % RUN_COLORS.length])} />
          <p className="muted small">Coût du jour tel que le planificateur le voit (heures sup. comprises au fil de la semaine, pénalités de report du jour).</p>
        </section>
      )}

      {ref && (
        <section className="card">
          <h3>Ouvrir une journée</h3>
          <div className="daygrid">
            {avail.map(({ r, b }) => (
              <div key={r.id} className="dayrow">
                <span className="dayrow-label">{r.label}</span>
                {b.days.map((d) => (
                  <button key={d.day} className="daybtn" onClick={() => onOpenDay(r.id, d.day)}>
                    <b>{DAYS[d.day].slice(0, 3)}</b>
                    <span>{d.routes.length} tournée{d.routes.length > 1 ? "s" : ""}</span>
                    <span className={d.postponed.length ? "warn" : ""}>{d.postponed.length ? `${d.postponed.length} report${d.postponed.length > 1 ? "s" : ""}` : "tout livré"}</span>
                  </button>
                ))}
              </div>
            ))}
          </div>
        </section>
      )}
    </div>
  );
}
