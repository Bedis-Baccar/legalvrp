import type { Ctx } from "../ClientApp";
import type { Bundle, Route, Segment } from "../../types";
import { CONTRACT_LABEL, DAYS, dur, hhmm, km, num, SEGMENT_LABEL, TYPE_LABEL } from "../../format";
import { customerName, driverColor, driverName, routeRows } from "../../util";
import Gantt, { Legend } from "../../components/Gantt";
import { dutyViolations } from "../model";
import { ViolationList } from "../../components/Verdict";

function weekOf(b: Bundle, driverId: string) {
  return b.days.map((d) => ({ d, r: d.routes.find((r) => r.driver_id === driverId) }));
}

export default function Drivers({ ctx }: { ctx: Ctx }) {
  return ctx.nav.driver ? <DriverDetail ctx={ctx} driverId={ctx.nav.driver} /> : <DriverGrid ctx={ctx} />;
}

function DriverGrid({ ctx }: { ctx: Ctx }) {
  const { bundle: b, go } = ctx;
  const contracts = new Map(b.instance.contracts.map((c) => [c.name, c]));
  return (
    <div className="c-cards">
      {b.instance.drivers.map((drv) => {
        const k = b.kpis.drivers.find((x) => x.driver_id === drv.id);
        const c = contracts.get(drv.contract_class)!;
        const week = weekOf(b, drv.id);
        const stops = week.reduce((s, x) => s + (x.r?.order_ids.length ?? 0), 0);
        const kms = week.reduce((s, x) => s + (x.r?.timeline.km ?? 0), 0);
        const hrs = k?.service_hours_week ?? 0;
        const truck = b.instance.trucks.find((t) => t.id === drv.truck_id);
        return (
          <button key={drv.id} className="card c-driver" onClick={() => go({ driver: drv.id })}>
            <div className="c-driver-head">
              <span className="c-avatar" style={{ background: driverColor(b, drv.id) }}>{(drv.display_name ?? drv.id).slice(0, 1)}</span>
              <div>
                <b>{driverName(b, drv.id)}</b>
                <div className="muted small">{CONTRACT_LABEL[drv.contract_class] ?? drv.contract_class} · prise de poste {hhmm(drv.shift_start)}</div>
              </div>
            </div>
            <div className="small">{truck?.id} · {truck?.capacity_pallets} palettes{truck?.has_tail_lift ? " · hayon" : ""}</div>
            <div className="c-meter" title={`${num(hrs)} h sur un seuil de ${num(c.weekly_threshold / 60)} h`}>
              <span style={{ width: `${Math.min(100, (100 * hrs * 60) / c.weekly_service_max)}%`, background: driverColor(b, drv.id) }} />
              <i style={{ left: `${(100 * c.weekly_threshold) / c.weekly_service_max}%` }} />
            </div>
            <div className="c-driver-stats">
              <span><b>{num(hrs)} h</b> service</span>
              <span><b>{k?.days_worked ?? 0}</b> jours</span>
              <span><b>{stops}</b> livraisons</span>
              <span><b>{km(kms)}</b></span>
            </div>
            <div className="c-weekdots">
              {week.map(({ d, r }) => (
                <span key={d.day} className={r ? "on" : ""} title={`${DAYS[d.day]} : ${r ? `${r.order_ids.length} arrêts` : "repos"}`}>{DAYS[d.day].slice(0, 1)}</span>
              ))}
            </div>
          </button>
        );
      })}
    </div>
  );
}

// Driving segments go to the next stop (or the depot); the feuille de route lists every task.
function taskPlace(b: Bundle, day: number, s: Segment): string {
  if (!s.order_id) return s.kind === "drive" ? "→ Dépôt" : "Dépôt";
  const d = b.days.find((x) => x.day === day)!;
  const o = d.orders.find((x) => x.id === s.order_id);
  const c = b.instance.customers.find((x) => x.id === o?.customer_id);
  return (s.kind === "drive" ? "→ " : "") + customerName(c);
}

function RouteSheet({ b, day, r }: { b: Bundle; day: number; r: Route }) {
  const d = b.days.find((x) => x.day === day)!;
  const rules = b.instance.rules;
  return (
    <table className="kpi c-sheet">
      <thead>
        <tr>
          <th>Début</th>
          <th>Fin</th>
          <th>Durée</th>
          <th>Tâche</th>
          <th>Lieu</th>
          <th>Détail</th>
          <th>Conduite depuis pause</th>
          <th>Travail depuis pause</th>
        </tr>
      </thead>
      <tbody>
        {r.timeline.segments.map((s, i) => {
          const o = s.order_id ? d.orders.find((x) => x.id === s.order_id) : undefined;
          const c = o ? b.instance.customers.find((x) => x.id === o.customer_id) : undefined;
          const detail =
            s.kind === "service" && o && c
              ? `${o.pallets} pal. · ${TYPE_LABEL[c.type]} · créneau ${hhmm(c.window_start)}–${hhmm(c.window_end)}${c.needs_tail_lift ? " · hayon" : ""}`
              : s.kind === "wait" && c
                ? `ouverture ${hhmm(c.window_start)}`
                : s.kind === "break"
                  ? `pause ${s.end - s.start} min`
                  : "";
          return (
            <tr key={i} className={`k-${s.kind}`}>
              <td>{hhmm(s.start)}</td>
              <td>{hhmm(s.end)}</td>
              <td>{dur(s.end - s.start)}</td>
              <td><span className={`c-task ${s.kind}`}>{SEGMENT_LABEL[s.kind]}</span></td>
              <td>{taskPlace(b, day, s)}</td>
              <td className="small">{detail}</td>
              <td><Counter v={s.drive_since_break} max={rules.drive_before_break} /></td>
              <td><Counter v={s.work_since_break} max={rules.work_before_break} /></td>
            </tr>
          );
        })}
      </tbody>
    </table>
  );
}

export function Counter({ v, max }: { v: number; max: number }) {
  const p = Math.min(1, v / max);
  const tone = v > max ? "bad" : max - v < 15 ? "tight" : "ok";
  return (
    <span className={`c-counter ${tone}`} title={`${dur(v)} / ${dur(max)}`}>
      <span className="bar"><span style={{ width: `${p * 100}%` }} /></span>
      {dur(v)}
    </span>
  );
}

function DriverDetail({ ctx, driverId }: { ctx: Ctx; driverId: string }) {
  const { bundle: b, nav, go } = ctx;
  const drv = b.instance.drivers.find((x) => x.id === driverId);
  if (!drv) return <div className="notice">Chauffeur inconnu.</div>;
  const week = weekOf(b, driverId);
  const k = b.kpis.drivers.find((x) => x.driver_id === driverId);
  const c = b.instance.contracts.find((x) => x.name === drv.contract_class)!;
  const truck = b.instance.trucks.find((t) => t.id === drv.truck_id);
  const sel = week.find((x) => x.d.day === nav.day && x.r) ?? week.find((x) => x.r);
  const rows = week
    .filter((x) => x.r)
    .map((x) => ({ ...routeRows(b, x.d, [x.r!])[0], key: String(x.d.day), label: DAYS[x.d.day], sublabel: `${x.r!.order_ids.length} arrêts · ${dur(x.r!.timeline.work)}` }));
  return (
    <div className="stack">
      <div className="toolbar no-print">
        <button className="link" onClick={() => go({ driver: undefined })}>← Tous les chauffeurs</button>
      </div>
      <section className="card c-driver-banner">
        <span className="c-avatar big" style={{ background: driverColor(b, driverId) }}>{(drv.display_name ?? drv.id).slice(0, 1)}</span>
        <div>
          <h2>{driverName(b, driverId)}</h2>
          <div className="muted">
            {CONTRACT_LABEL[drv.contract_class]} · prise de poste {hhmm(drv.shift_start)}, fin au plus tard {hhmm(drv.shift_end_max)} · {truck?.id} ({truck?.capacity_pallets} pal.{truck?.has_tail_lift ? ", hayon" : ""})
          </div>
        </div>
        <div className="c-banner-stats">
          <div><b>{num(k?.service_hours_week ?? 0)} h</b><span>service (seuil {num(c.weekly_threshold / 60)} h, plafond {num(c.weekly_service_max / 60)} h)</span></div>
          <div><b>{num(k?.driving_hours_week ?? 0)} h</b><span>conduite</span></div>
          <div><b>{k?.extra_minutes ?? 0} min</b><span>au-delà du seuil</span></div>
          <div><b>{k?.days_worked ?? 0}</b><span>jours travaillés</span></div>
        </div>
      </section>
      <section className="card no-print">
        <h3>La semaine</h3>
        <Legend />
        {rows.length ? <Gantt rows={rows} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} /> : <div className="notice">Aucune tournée cette semaine.</div>}
      </section>
      {sel?.r && (
        <section className="card c-print">
          <div className="panel-head">
            <h3>Feuille de route — {DAYS[sel.d.day]}</h3>
            <div className="toolbar no-print">
              <div className="seg-control small">
                {week.filter((x) => x.r).map((x) => (
                  <button key={x.d.day} className={x.d.day === sel.d.day ? "on" : ""} onClick={() => go({ day: x.d.day })}>
                    {DAYS[x.d.day].slice(0, 3)}
                  </button>
                ))}
              </div>
              <button className="btn" onClick={() => window.print()}>Imprimer</button>
            </div>
          </div>
          <p className="small muted">
            {sel.r.order_ids.length} livraisons · {sel.r.timeline.pallets} palettes · {km(sel.r.timeline.km ?? 0)} · conduite {dur(sel.r.timeline.driving)} · service {dur(sel.r.timeline.work)} · pauses {dur(sel.r.timeline.breaks)}
          </p>
          <div className="table-wrap">
            <RouteSheet b={b} day={sel.d.day} r={sel.r} />
          </div>
          <h3 style={{ marginTop: "1rem" }}>Verdict du vérificateur</h3>
          <ViolationList violations={dutyViolations(sel.d, driverId)} bundle={b} day={sel.d} />
          <p className="no-print">
            <button className="link" onClick={() => go({ page: "audit", driver: driverId, day: sel.d.day })}>Contrôle détaillé de cette tournée →</button>
          </p>
        </section>
      )}
    </div>
  );
}
