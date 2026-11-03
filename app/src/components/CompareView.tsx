// School cases: several plans of the same small day, each judged by the checker.
import type { ScenarioEntry } from "../types";
import { eur } from "../format";
import { customerName, Loading, routeRows } from "../util";
import Gantt, { Legend } from "./Gantt";
import { ViolationList } from "./Verdict";
import { useCompareRuns } from "./WeekView";

export default function CompareView({ sc }: { sc: ScenarioEntry }) {
  const { runs, bundles, error } = useCompareRuns(sc);
  if (!bundles) return <Loading error={error} />;
  const from = Math.min(...bundles.flatMap((b) => b?.days.flatMap((d) => d.routes.map((r) => r.timeline.duty_start)) ?? [360])) - 30;
  const to = Math.max(...bundles.flatMap((b) => b?.days.flatMap((d) => d.routes.map((r) => r.timeline.duty_end)) ?? [1140])) + 30;
  return (
    <div className="stack">
      <Legend />
      <div className="compare-grid">
        {runs.map((r, i) => {
          const b = bundles[i];
          if (!b) return <div key={r.id} className="notice">{r.label} : {r.unavailable ?? "indisponible"}</div>;
          const d = b.days[0];
          const legal = d.check.ok;
          const cust = new Map(b.instance.customers.map((c) => [c.id, c]));
          return (
            <section key={r.id} className={`card case ${legal ? "legal" : "illegal"}`}>
              <div className="case-head">
                <h3>{r.label}</h3>
                <span className={legal ? "badge ok" : "badge bad"}>{legal ? "légal" : "illégal"}</span>
              </div>
              {r.note && <p className="small muted">{r.note}</p>}
              <div className="small">
                {d.routes.map((rt) => (
                  <span key={rt.driver_id}>
                    {rt.order_ids.map((o) => customerName(cust.get(d.orders.find((x) => x.id === o)?.customer_id ?? ""))).join(" → ")}
                    {rt.breaks.length ? ` · pauses : ${rt.breaks.map((x) => `${x.minutes} min après ${customerName(cust.get(d.orders.find((o) => o.id === x.after)?.customer_id ?? ""))}`).join(", ")}` : " · sans pause"}
                  </span>
                ))}
                {d.postponed.length > 0 && <span className="warn"> · reporté : {d.postponed.map((o) => customerName(cust.get(d.orders.find((x) => x.id === o)?.customer_id ?? ""))).join(", ")}</span>}
              </div>
              {d.routes.length > 0 && <Gantt rows={routeRows(b, d, d.routes)} from={from} to={to} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} />}
              <ViolationList violations={d.check.violations} bundle={b} day={d} />
              <div className="small muted">
                {legal ? `Coût du jour ${eur(d.check.objective)}${d.check.postponement_cost ? ` (dont report ${eur(d.check.postponement_cost)})` : ""} · ${b.solver === "hand" ? "plan fait main" : "planificateur " + b.solver.toUpperCase()}` : "Plan rejeté : il ne peut pas être exécuté."}
              </div>
            </section>
          );
        })}
      </div>
    </div>
  );
}
