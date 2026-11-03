import { useCallback, useState } from "react";
import type { Ctx } from "../ClientApp";
import { DAYS, dur, eur, hhmm, km, TYPE_LABEL } from "../../format";
import { customerName, driverColor, driverName, routeRows } from "../../util";
import Gantt, { Legend } from "../../components/Gantt";
import RouteMap from "../../components/RouteMap";

export function DayTabs({ ctx }: { ctx: Ctx }) {
  const { bundle: b, nav, go } = ctx;
  return (
    <div className="seg-control">
      {b.days.map((d) => (
        <button key={d.day} className={d.day === nav.day ? "on" : ""} onClick={() => go({ day: d.day })}>
          {DAYS[d.day]}
          {d.postponed.length > 0 && <span className="pill bad">{d.postponed.length}</span>}
        </button>
      ))}
    </div>
  );
}

export default function Planning({ ctx }: { ctx: Ctx }) {
  const { bundle: b, nav, go } = ctx;
  const d = b.days.find((x) => x.day === nav.day) ?? b.days[0];
  const [sel, setSel] = useState<string>();
  const onSel = useCallback((o: string) => setSel((x) => (x === o ? undefined : o)), []);
  const driver = nav.driver && d.routes.some((r) => r.driver_id === nav.driver) ? nav.driver : d.routes.find((r) => sel && r.order_ids.includes(sel))?.driver_id;
  const order = sel ? d.orders.find((o) => o.id === sel) : undefined;
  const cust = order ? b.instance.customers.find((c) => c.id === order.customer_id) : undefined;
  const r = order ? d.routes.find((x) => x.order_ids.includes(order.id)) : undefined;
  const i = r && order ? r.order_ids.indexOf(order.id) : -1;
  const kmDay = d.routes.reduce((s, x) => s + (x.timeline.km ?? 0), 0);
  return (
    <div className="stack">
      <div className="toolbar">
        <DayTabs ctx={ctx} />
        {nav.driver && (
          <button className="chip" onClick={() => go({ driver: undefined })}>
            {driverName(b, nav.driver)} ✕
          </button>
        )}
      </div>
      <div className="c-tiles small">
        <div className="c-tile"><div className="c-tile-label">Commandes</div><div className="c-tile-value">{d.orders.length}</div><div className="c-tile-sub">{d.orders.filter((o) => o.postponed_from !== undefined).length} reportée(s) de la veille</div></div>
        <div className="c-tile"><div className="c-tile-label">Tournées</div><div className="c-tile-value">{d.routes.length}</div><div className="c-tile-sub">sur {d.drivers.length} chauffeurs</div></div>
        <div className="c-tile"><div className="c-tile-label">Distance</div><div className="c-tile-value">{km(kmDay)}</div></div>
        <div className="c-tile"><div className="c-tile-label">Coût du jour</div><div className="c-tile-value">{eur(d.check.objective)}</div></div>
        <div className={`c-tile ${d.postponed.length ? "warn" : "ok"}`}><div className="c-tile-label">Reportées</div><div className="c-tile-value">{d.postponed.length}</div></div>
        <div className={`c-tile ${d.check.ok ? "ok" : "bad"}`}><div className="c-tile-label">Vérificateur</div><div className="c-tile-value">{d.check.ok ? "conforme" : `${d.check.violations.length} infr.`}</div></div>
      </div>
      <div className="c-plan">
        <section className="card c-mapcard">
          <RouteMap bundle={b} day={d} routes={d.routes} tiles={true} selectedOrder={sel} selectedDriver={driver} onSelectOrder={onSel} highlight="tail_lift" />
        </section>
        <section className="card c-routes">
          {order && cust ? (
            <div className="stack tight">
              <div className="panel-head">
                <h3>{customerName(cust)}</h3>
                <button className="link" onClick={() => setSel(undefined)}>✕</button>
              </div>
              <dl className="facts">
                <dt>Commande</dt><dd>{order.id} · {order.pallets} pal. · {TYPE_LABEL[cust.type]}</dd>
                <dt>Créneau</dt><dd>{hhmm(cust.window_start)} – {hhmm(cust.window_end)}</dd>
                <dt>Hayon</dt><dd>{cust.needs_tail_lift ? "obligatoire" : "non"}</dd>
                <dt>Livraison</dt><dd>{r ? `${driverName(b, r.driver_id)}, arrêt ${i + 1}, ${hhmm(r.service_starts[i])} (${dur(order.service_planned)})` : "reportée"}</dd>
                {r && <><dt>Marge créneau</dt><dd>{dur(cust.window_end - r.service_starts[i])}</dd></>}
              </dl>
              <button className="btn" onClick={() => go({ page: "orders", order: order.id })}>Fiche commande →</button>
            </div>
          ) : (
            <>
              <h3>Tournées</h3>
              <ul className="c-routelist">
                {d.routes.map((x) => (
                  <li key={x.driver_id} className={driver === x.driver_id ? "on" : ""}>
                    <button onClick={() => go({ driver: nav.driver === x.driver_id ? undefined : x.driver_id })}>
                      <span className="dot" style={{ background: driverColor(b, x.driver_id) }} />
                      <b>{driverName(b, x.driver_id)}</b>
                      <span className="muted small">{x.truck_id} · {x.order_ids.length} arrêts · {x.timeline.pallets} pal. · {km(x.timeline.km ?? 0)} · {hhmm(x.timeline.duty_start)}–{hhmm(x.timeline.duty_end)}</span>
                    </button>
                    <button className="link small" onClick={() => go({ page: "drivers", driver: x.driver_id })}>Feuille de route →</button>
                  </li>
                ))}
              </ul>
              {d.postponed.length > 0 && (
                <>
                  <h3>Reportées</h3>
                  <ul className="plain small">
                    {d.postponed.map((o) => {
                      const ord = d.orders.find((x) => x.id === o)!;
                      const c = b.instance.customers.find((x) => x.id === ord.customer_id);
                      return (
                        <li key={o}>
                          <button className="stop" onClick={() => onSel(o)}>{customerName(c)}</button> {ord.pallets} pal.{c?.needs_tail_lift ? " · hayon" : ""} · pénalité {eur(ord.penalty)}
                        </li>
                      );
                    })}
                  </ul>
                </>
              )}
            </>
          )}
        </section>
      </div>
      <section className="card">
        <h3>Planning des chauffeurs — {DAYS[d.day]}</h3>
        <Legend />
        <Gantt rows={routeRows(b, d, driver ? d.routes.filter((x) => x.driver_id === driver) : d.routes)} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} selectedOrder={sel} onSelectOrder={onSel} />
      </section>
    </div>
  );
}
