import type { Ctx } from "../ClientApp";
import type { Bundle } from "../../types";
import { DAYS, eur, km, num, pct } from "../../format";
import { HoursChart } from "../../components/DriversView";
import { costParts, fillRate, orderFates, realisedTally } from "../model";
import { customerName, driverName } from "../../util";

function Tile({ label, value, sub, delta, good, tone }: { label: string; value: string; sub?: string; delta?: number; good?: "low" | "high"; tone?: "ok" | "bad" | "warn" }) {
  const better = delta === undefined || !good ? undefined : good === "low" ? delta < 0 : delta > 0;
  return (
    <div className={`c-tile ${tone ?? ""}`}>
      <div className="c-tile-label">{label}</div>
      <div className="c-tile-value">{value}</div>
      <div className="c-tile-sub">
        {delta !== undefined && Math.abs(delta) > 0.0005 && <span className={better ? "delta good" : "delta bad"}>{pct(delta)} </span>}
        {sub}
      </div>
    </div>
  );
}

function CostBars({ rows }: { rows: { label: string; b: Bundle }[] }) {
  const max = Math.max(...rows.map((r) => costParts(r.b).total));
  return (
    <div className="c-costbars">
      {rows.map(({ label, b }) => {
        const p = costParts(b);
        const w = (v: number) => `${(100 * v) / max}%`;
        return (
          <div key={label} className="c-costrow">
            <div className="c-costlabel">{label}</div>
            <div className="c-costbar">
              <span className="km" style={{ width: w(p.km) }} title={`Kilomètres ${eur(p.km)}`} />
              <span className="staff" style={{ width: w(p.staff) }} title={`Personnel ${eur(p.staff)}`} />
              <span className="pen" style={{ width: w(p.penalties) }} title={`Pénalités de report ${eur(p.penalties)}`} />
            </div>
            <div className="c-costtotal">{eur(p.total)}</div>
          </div>
        );
      })}
      <div className="legend">
        <span><i style={{ background: "var(--c-km)" }} /> Kilomètres</span>
        <span><i style={{ background: "var(--c-staff)" }} /> Personnel (heures, heures sup., intérim)</span>
        <span><i style={{ background: "var(--c-pen)" }} /> Pénalités de report</span>
      </div>
    </div>
  );
}

export default function Dashboard({ ctx }: { ctx: Ctx }) {
  const { bundle: b, reference: ref, run, go } = ctx;
  const k = b.kpis;
  const rel = (f: (x: Bundle) => number) => (ref ? (f(b) - f(ref)) / Math.max(1e-9, Math.abs(f(ref))) : undefined);
  const fates = orderFates(b);
  const total = fates.length;
  const risk = realisedTally(b);
  const fill = fillRate(b);
  const contracts = new Map(b.instance.contracts.map((c) => [c.name, c]));
  const nearCap = k.drivers.filter((d) => {
    const c = contracts.get(d.contract);
    return c && d.service_hours_week * 60 > c.weekly_threshold - 60;
  });
  const postponed = fates.filter((f) => f.postponedOn.length);
  const extraHours = k.extra_minutes_total / 60;

  return (
    <div className="stack">
      <div className="notice info small">
        Plan affiché : <b>{run.label}</b>
        {ref ? " — comparé au planning actuel (secteurs fixes)." : "."} Toutes les tournées sont rejugées par le vérificateur indépendant.
        {run.note && <div className="muted">{run.note}</div>}
      </div>
      <div className="c-tiles">
        <Tile label="Coût de la semaine" value={eur(k.cost_total)} delta={rel((x) => x.kpis.cost_total)} good="low" sub="coûts illustratifs" />
        <Tile label="Kilomètres" value={km(k.km)} delta={rel((x) => x.kpis.km)} good="low" />
        <Tile label="Commandes livrées" value={`${k.served} / ${total}`} sub={k.unserved_end ? `${k.unserved_end} non livrée(s) vendredi` : "toutes livrées"} tone={k.unserved_end ? "warn" : "ok"} />
        <Tile label="Reports" value={`${k.postponement_decisions}`} sub={ref ? `contre ${ref.kpis.postponement_decisions} aujourd'hui` : undefined} tone={k.postponement_decisions ? "warn" : "ok"} />
        <Tile label="Conformité légale" value={b.week_check.ok ? "100 %" : `${b.week_check.violations.length} infr.`} sub={`${b.days.reduce((s, d) => s + d.routes.length, 0)} tournées contrôlées`} tone={b.week_check.ok ? "ok" : "bad"} />
        <Tile label="Remplissage des camions" value={`${num(fill * 100, 0)} %`} sub="palettes / capacité, par tournée" />
        <Tile label="Heures sup. et complémentaires" value={`${num(extraHours)} h`} sub={ref ? `contre ${num(ref.kpis.extra_minutes_total / 60)} h` : undefined} />
        <Tile label="Tournées à risque (temps réels)" value={`${risk.either} / ${risk.duties}`} sub="en retard ou illégales une fois exécutées" tone={risk.duties && risk.either / risk.duties > 0.25 ? "bad" : risk.either ? "warn" : "ok"} />
      </div>

      <div className="c-grid2">
        <section className="card">
          <h3>Décomposition du coût</h3>
          <CostBars rows={[...(ref ? [{ label: "Planning actuel", b: ref }] : []), { label: run.label, b }]} />
          <p className="muted small">Les pénalités de report (200 €, puis 400 €, 800 €… ; 1 000 € pour une commande non livrée le vendredi) sont des valeurs illustratives : le gain « physique » se lit dans les kilomètres et le nombre de reports.</p>
        </section>
        <section className="card">
          <h3>Points d'attention</h3>
          <ul className="c-alerts">
            {postponed.length > 0 && (
              <li className="warn">
                <b>{postponed.length} commande(s) reportée(s)</b> :{" "}
                {postponed.slice(0, 4).map((f, i) => (
                  <span key={f.id}>
                    {i > 0 && ", "}
                    <a href="#" onClick={(e) => { e.preventDefault(); go({ page: "orders", order: f.id }); }}>{customerName(f.customer)}</a>
                  </span>
                ))}
                {postponed.length > 4 ? "…" : ""}
              </li>
            )}
            {k.unserved_end > 0 && <li className="bad"><b>{k.unserved_end} commande(s) non livrée(s)</b> en fin de semaine.</li>}
            {nearCap.map((d) => (
              <li key={d.driver_id} className="warn">
                <b>{driverName(b, d.driver_id)}</b> : {num(d.service_hours_week)} h sur un seuil de {num(d.threshold_hours)} h.
              </li>
            ))}
            {risk.either > 0 && (
              <li className={risk.either / Math.max(1, risk.duties) > 0.25 ? "bad" : "warn"}>
                <b>{risk.either} tournée(s) sur {risk.duties}</b> en retard ou illégales si les livraisons durent ce qu'elles durent vraiment.{" "}
                <a href="#" onClick={(e) => { e.preventDefault(); go({ page: "audit", real: true }); }}>Voir le contrôle</a>
                {run.solver !== "baseline" && ctx.sc.runs.some((r) => r.id !== run.id && r.label.includes("marge") && r.file) && (
                  <> — ou passez au plan <a href="#" onClick={(e) => { e.preventDefault(); go({ plan: ctx.sc.runs.find((r) => r.label.includes("marge") && r.file)!.id }); }}>« {ctx.sc.runs.find((r) => r.label.includes("marge") && r.file)!.label} »</a>.</>
                )}
              </li>
            )}
            {!postponed.length && !k.unserved_end && !nearCap.length && !risk.either && <li className="ok">Aucun point d'attention.</li>}
          </ul>
        </section>
      </div>

      <section className="card">
        <h3>Jour par jour</h3>
        <div className="table-wrap">
          <table className="kpi c-days">
            <thead>
              <tr>
                <th>Jour</th>
                <th>Commandes</th>
                <th>Livrées</th>
                <th>Reportées</th>
                <th>Chauffeurs</th>
                <th>Km</th>
                <th>Coût</th>
                <th>Vérificateur</th>
                <th>Risque (temps réels)</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {b.days.map((d) => {
                const kd = k.days.find((x) => x.day === d.day)!;
                const rt = realisedTally(b, d.day);
                return (
                  <tr key={d.day}>
                    <td><b>{DAYS[d.day]}</b></td>
                    <td>{d.orders.length}</td>
                    <td>{kd.served}</td>
                    <td className={d.postponed.length ? "warn" : ""}>{d.postponed.length}</td>
                    <td>{d.routes.length} / {d.drivers.length}</td>
                    <td>{km(kd.km)}</td>
                    <td>{eur(kd.cost)}</td>
                    <td>{d.check.ok ? <span className="badge ok">conforme</span> : <span className="badge bad">{d.check.violations.length}</span>}</td>
                    <td>{rt.either ? <span className={rt.either / rt.duties > 0.25 ? "badge bad" : "badge warnb"}>{rt.either} / {rt.duties}</span> : <span className="badge ok">0</span>}</td>
                    <td><button className="link" onClick={() => go({ page: "planning", day: d.day })}>Ouvrir →</button></td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
      </section>

      <section className="card">
        <h3>Heures de la semaine par chauffeur</h3>
        <div className="legend">
          <span>Une nuance par jour (lundi le plus foncé)</span>
          <span><i className="threshold" /> seuil du contrat</span>
          <span><i className="cap" /> plafond</span>
        </div>
        <HoursChart b={b} />
      </section>
    </div>
  );
}
