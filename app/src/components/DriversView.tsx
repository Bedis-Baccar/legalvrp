// Weekly hours per driver: day by day, against the contract threshold and the cap.
import type { Bundle, ScenarioEntry } from "../types";
import { CONTRACT_LABEL, DAYS, num } from "../format";
import { driverColor, driverName, Loading, useWidth } from "../util";
import { RUN_COLORS } from "./Charts";
import { useCompareRuns } from "./WeekView";

function HoursChart({ b }: { b: Bundle }) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const drivers = b.instance.drivers;
  const contracts = new Map(b.instance.contracts.map((c) => [c.name, c]));
  const maxH = Math.max(...drivers.map((d) => (contracts.get(d.contract_class)?.weekly_service_max ?? 0) / 60), 10);
  const left = 160;
  const rowH = 34;
  const h = drivers.length * rowH + 24;
  const x = (hrs: number) => left + (hrs / maxH) * (width - left - 16);
  const dayShades = [1, 0.82, 0.66, 0.5, 0.36, 0.25, 0.15];
  return (
    <div ref={ref} className="chart">
      <svg width={width} height={h}>
        {[0, 10, 20, 30, 40, 50].filter((v) => v <= maxH).map((v) => (
          <g key={v}>
            <line x1={x(v)} x2={x(v)} y1={0} y2={h - 18} className="grid" />
            <text x={x(v)} y={h - 4} className="axis" textAnchor="middle">
              {v} h
            </text>
          </g>
        ))}
        {drivers.map((d, i) => {
          const y = i * rowH + 4;
          const c = contracts.get(d.contract_class);
          let acc = 0;
          return (
            <g key={d.id}>
              <text x={6} y={y + 13} className="rowlabel">
                {driverName(b, d.id)}
              </text>
              <text x={6} y={y + 26} className="rowsub">
                {CONTRACT_LABEL[d.contract_class] ?? d.contract_class}
              </text>
              {b.kpis.days.map((k, j) => {
                const hrs = (k.service_minutes[d.id] ?? 0) / 60;
                const x0 = x(acc);
                acc += hrs;
                if (!hrs) return null;
                return (
                  <rect key={j} x={x0} y={y + 4} width={Math.max(0.5, x(acc) - x0 - 1)} height={20} fill={driverColor(b, d.id)} opacity={dayShades[j] ?? 0.2}>
                    <title>{`${DAYS[k.day]} : ${num(hrs)} h`}</title>
                  </rect>
                );
              })}
              {c && (
                <>
                  <line x1={x(c.weekly_threshold / 60)} x2={x(c.weekly_threshold / 60)} y1={y} y2={y + 28} className="threshold">
                    <title>{`Seuil contractuel ${num(c.weekly_threshold / 60)} h : au-delà, heures supplémentaires`}</title>
                  </line>
                  <line x1={x(c.weekly_service_max / 60)} x2={x(c.weekly_service_max / 60)} y1={y} y2={y + 28} className="cap">
                    <title>{`Plafond ${num(c.weekly_service_max / 60)} h`}</title>
                  </line>
                </>
              )}
              <text x={x(acc) + 6} y={y + 18} className="barlabel" textAnchor="start">
                {num(acc)} h
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
}

export default function DriversView({ sc }: { sc: ScenarioEntry }) {
  const { runs, bundles, error } = useCompareRuns(sc);
  if (!bundles) return <Loading error={error} />;
  return (
    <div className="stack">
      {runs.map((r, i) => {
        const b = bundles[i];
        if (!b) return null;
        return (
          <section key={r.id} className="card">
            <h3>
              <span className="dot" style={{ background: RUN_COLORS[i % RUN_COLORS.length] }} /> {r.label}
            </h3>
            <div className="legend">
              <span>Une nuance par jour (lundi le plus foncé)</span>
              <span>
                <i className="threshold" /> seuil du contrat
              </span>
              <span>
                <i className="cap" /> plafond
              </span>
              <span>
                Gini temps plein : <b>{num(b.kpis.hours_gini, 3)}</b>
              </span>
              <span>
                Heures sup. : <b>{num(b.kpis.extra_minutes_total / 60)} h</b>
              </span>
            </div>
            <HoursChart b={b} />
          </section>
        );
      })}
    </div>
  );
}
