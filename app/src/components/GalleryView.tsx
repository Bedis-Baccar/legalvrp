// The checker's gallery: the labelled duty corpus, each duty with its timeline and verdict.
import { useMemo, useState } from "react";
import type { GalleryItem, ScenarioEntry } from "../types";
import { dur, hhmm, ruleTitle } from "../format";
import { Loading } from "../util";
import { useGallery } from "../data";
import Gantt, { Legend } from "./Gantt";
import { amountText } from "./Verdict";

type Filter = "featured" | "legal" | "illegal" | "all";

export default function GalleryView({ sc }: { sc: ScenarioEntry }) {
  const { data: g, error } = useGallery(sc.gallery?.file);
  const featured = sc.gallery?.featured ?? [];
  const [filter, setFilter] = useState<Filter>(featured.length ? "featured" : "all");
  const [sel, setSel] = useState<string>();
  const items = useMemo(() => {
    if (!g) return [];
    if (filter === "featured") return featured.map((n) => g.items.find((i) => i.name === n)).filter((x): x is GalleryItem => !!x);
    if (filter === "legal") return g.items.filter((i) => !i.expect.length);
    if (filter === "illegal") return g.items.filter((i) => i.expect.length);
    return g.items;
  }, [g, filter, featured]);
  if (!g) return <Loading error={error} />;
  const item = g.items.find((i) => i.name === sel) ?? items[0];
  const matches = g.items.filter((i) => i.matches).length;
  return (
    <div className="stack">
      <div className="stats">
        <div className="stat good">
          <div className="stat-label">Verdicts conformes aux étiquettes</div>
          <div className="stat-value">
            {matches} / {g.items.length}
          </div>
        </div>
        <div className="stat">
          <div className="stat-label">Tournées légales</div>
          <div className="stat-value">{g.items.filter((i) => !i.expect.length).length}</div>
        </div>
        <div className="stat">
          <div className="stat-label">Tournées illégales</div>
          <div className="stat-value">{g.items.filter((i) => i.expect.length).length}</div>
        </div>
      </div>
      <div className="gallery">
        <section className="card list">
          <div className="seg-control small">
            {(["featured", "illegal", "legal", "all"] as Filter[]).filter((f) => f !== "featured" || featured.length).map((f) => (
              <button key={f} className={filter === f ? "on" : ""} onClick={() => setFilter(f)}>
                {{ featured: "À voir", legal: "Légales", illegal: "Illégales", all: "Toutes" }[f]}
              </button>
            ))}
          </div>
          <ul className="gallery-list">
            {items.map((i) => (
              <li key={i.name}>
                <button className={i === item ? "on" : ""} onClick={() => setSel(i.name)}>
                  <span className={i.expect.length ? "badge bad" : "badge ok"}>{i.expect.length ? "illégale" : "légale"}</span> {i.name}
                </button>
              </li>
            ))}
          </ul>
        </section>
        {item && (
          <section className="card">
            <h3>{item.name}</h3>
            <p className="small muted">Libellé d'origine (anglais) du corpus de tests ; trajets imposés, chauffeur seul, service {hhmm(item.shift.start)} – {hhmm(item.shift.end_max)}.</p>
            <div className="table-wrap">
              <table className="kpi compact">
                <thead>
                  <tr>
                    <th>Arrêt</th>
                    <th>Trajet pour y aller</th>
                    <th>Créneau</th>
                    <th>Livraison</th>
                    <th>Début prévu</th>
                    <th>Pause après</th>
                  </tr>
                </thead>
                <tbody>
                  {item.stops.map((s, k) => {
                    const b = item.route.breaks.find((x) => x.after === s.id);
                    return (
                      <tr key={s.id}>
                        <td>{s.id}</td>
                        <td>{dur(item.legs[k])}</td>
                        <td>
                          {hhmm(s.window_start)} – {hhmm(s.window_end)}
                        </td>
                        <td>{dur(s.service)}</td>
                        <td>{hhmm(item.route.service_starts[k])}</td>
                        <td>{b ? `${b.minutes} min` : ""}</td>
                      </tr>
                    );
                  })}
                  {item.legs.length > 0 && (
                    <tr>
                      <td>retour</td>
                      <td>{dur(item.legs[item.legs.length - 1])}</td>
                      <td colSpan={4} />
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
            {(item.state.service_minutes_week > 0 || item.state.driving_minutes_week > 0) && (
              <p className="small">
                Déjà dans la semaine : {dur(item.state.service_minutes_week)} de service, {dur(item.state.driving_minutes_week)} de conduite.
              </p>
            )}
            {item.timeline && (
              <>
                <Legend />
                <Gantt
                  rows={[{
                    key: "k1", label: "Chauffeur", sublabel: `${dur(item.timeline.driving)} cond. · ${dur(item.timeline.work)} trav.`, color: "#2563eb",
                    timeline: item.timeline, windows: Object.fromEntries(item.stops.map((s) => [s.id, [s.window_start, s.window_end] as [number, number]])),
                    shiftEnd: item.shift.end_max,
                  }]}
                  from={Math.floor(item.timeline.duty_start / 60) * 60 - 30}
                  to={Math.max(item.timeline.duty_end, item.shift.end_max) + 30}
                  limits={{ drive: g.rules.drive_before_break, work: g.rules.work_before_break }}
                />
              </>
            )}
            <div className="verdicts">
              <div>
                <div className="stat-label">Étiquette attendue</div>
                {item.expect.length ? item.expect.map((r) => <div key={r} className="badge bad">{ruleTitle(r)}</div>) : <div className="badge ok">légale</div>}
              </div>
              <div>
                <div className="stat-label">Verdict du vérificateur</div>
                {item.violations.length ? (
                  item.violations.map((v, k) => (
                    <div key={k} className="badge bad">
                      {ruleTitle(v.rule)} {amountText(v) && <span className="small">({amountText(v)})</span>}
                    </div>
                  ))
                ) : (
                  <div className="badge ok">légale</div>
                )}
              </div>
              <div className={item.matches ? "match ok" : "match bad"}>{item.matches ? "✓ identique" : "✗ différent"}</div>
            </div>
          </section>
        )}
      </div>
    </div>
  );
}
