// Frozen results computed with Gurobi (look-ahead policies, V1-T5).
import type { ScenarioEntry } from "../types";
import { eur, pct } from "../format";
import { GroupedBars, SeriesLegend } from "./Charts";

export default function ReportView({ sc }: { sc: ScenarioEntry }) {
  const rep = sc.report;
  if (!rep) return null;
  const rows = rep.rows;
  const colors = ["#c2255c", "#2563eb", "#0f9d58"];
  const sum = (k: "myopic" | "LA2" | "clairvoyant") => rows.reduce((s, r) => s + (r[k] ?? 0), 0);
  const recovery = (sum("myopic") - sum("LA2")) / Math.max(1e-9, sum("myopic") - sum("clairvoyant"));
  return (
    <div className="stack">
      <section className="card">
        <h3>Coût de la semaine selon ce que le planificateur sait</h3>
        <SeriesLegend labels={["Jour par jour (myope)", "Aujourd'hui + demain", "Toute la semaine (clairvoyant)"]} colors={colors} />
        <GroupedBars groups={rows.map((r) => `Semaine ${r.seed}`)} colors={colors} format={eur}
          series={[
            { label: "Jour par jour", values: rows.map((r) => r.myopic) },
            { label: "Aujourd'hui + demain", values: rows.map((r) => r.LA2) },
            { label: "Clairvoyant", values: rows.map((r) => r.clairvoyant) },
          ]} />
        <div className="table-wrap">
          <table className="kpi">
            <thead>
              <tr>
                <th>Semaine</th>
                <th>Jour par jour</th>
                <th>Aujourd'hui + demain</th>
                <th>Clairvoyant</th>
                <th>Perte de la myopie</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => (
                <tr key={r.seed}>
                  <td>{r.seed}</td>
                  <td>{eur(r.myopic)}</td>
                  <td>{eur(r.LA2)}</td>
                  <td>{eur(r.clairvoyant)}</td>
                  <td>{pct((r.myopic - r.clairvoyant) / r.clairvoyant)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <p className="small">
          Sur les cinq semaines, regarder le lendemain récupère <b>{Math.round(recovery * 100)} %</b> de l'écart entre jour par jour et clairvoyant.
        </p>
        <p className="muted small">Source : {rep.source} (MILP Gurobi, V1-T5 ; chaque semaine validée par le vérificateur). Résultats figés, non recalculés par la démo.</p>
      </section>
    </div>
  );
}
