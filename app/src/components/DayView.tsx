// One day of one run: map, drivers' timelines, the orders and the checker's verdict.
import { useCallback, useMemo, useState } from "react";
import type { Bundle, Day, ScenarioEntry } from "../types";
import { useBundle } from "../data";
import { DAYS, dur, eur, hhmm, km, TYPE_LABEL } from "../format";
import { customerMap, customerName, driverColor, driverName, Loading, routeRows } from "../util";
import Gantt, { Legend } from "./Gantt";
import RouteMap from "./RouteMap";
import { ViolationList } from "./Verdict";

interface Props {
  sc: ScenarioEntry;
  run: string;
  day: number;
  onChange: (run: string, day: number) => void;
  tiles: boolean;
}

export default function DayView({ sc, run, day, onChange, tiles }: Props) {
  const runs = sc.runs.filter((r) => r.file);
  const entry = runs.find((r) => r.id === run) ?? runs[0];
  const { data: b, error } = useBundle(entry?.file);
  const focus = sc.focus.find((f) => f.run === entry?.id || !f.run);
  const [selOrder, setSelOrder] = useState<string>();
  const onSelectOrder = useCallback((o: string) => setSelOrder((x) => (x === o ? undefined : o)), []);
  const d = b?.days.find((x) => x.day === day) ?? b?.days[0];
  const selDriver = useMemo(() => d?.routes.find((r) => selOrder && r.order_ids.includes(selOrder))?.driver_id, [d, selOrder]);

  if (!entry) return <div className="notice">Aucun plan disponible pour ce scénario.</div>;
  return (
    <div className="stack">
      <div className="toolbar">
        <div className="seg-control">
          {runs.map((r) => (
            <button key={r.id} className={r.id === entry.id ? "on" : ""} onClick={() => onChange(r.id, day)}>
              {r.label}
            </button>
          ))}
        </div>
        {b && b.days.length > 1 && (
          <div className="seg-control">
            {b.days.map((x) => (
              <button key={x.day} className={x.day === d?.day ? "on" : ""} onClick={() => onChange(entry.id, x.day)}>
                {DAYS[x.day]}
              </button>
            ))}
          </div>
        )}
      </div>
      {!b || !d ? (
        <Loading error={error} />
      ) : (
        <>
          {focus?.text && <div className="notice info">{focus.text}</div>}
          <DaySummary b={b} d={d} />
          <div className="daylayout">
            <section className="card mapcard">
              <RouteMap bundle={b} day={d} routes={d.routes} tiles={tiles} highlight={focus?.highlight} selectedOrder={selOrder}
                selectedDriver={selDriver} onSelectOrder={onSelectOrder} />
              <p className="muted small">
                {b.geo?.synthetic ? "Trajets schématiques (lignes droites) sur une géographie synthétique. " : ""}Cercles pointillés : commandes reportées.
                {focus?.highlight === "tail_lift" ? " Cercles noirs épais : clients à hayon." : ""}
              </p>
            </section>
            <section className="card side">
              {selOrder ? <OrderPanel b={b} d={d} orderId={selOrder} onClose={() => setSelOrder(undefined)} /> : <DayPanel b={b} d={d} onSelectOrder={onSelectOrder} />}
            </section>
          </div>
          <section className="card">
            <h3>Planning des chauffeurs — {DAYS[d.day]}</h3>
            <Legend />
            {d.routes.length ? (
              <Gantt rows={routeRows(b, d, d.routes)} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }}
                selectedOrder={selOrder} onSelectOrder={onSelectOrder} />
            ) : (
              <div className="notice">Aucune tournée ce jour.</div>
            )}
            <p className="muted small">Survolez un segment : heures, créneau du client et compteurs de la loi (conduite depuis la pause ≤ 4 h 30, travail depuis la pause ≤ 6 h). Trait rouge vertical : fin de service autorisée.</p>
          </section>
        </>
      )}
    </div>
  );
}

function DaySummary({ b, d }: { b: Bundle; d: Day }) {
  const served = d.routes.reduce((s, r) => s + r.order_ids.length, 0);
  const kmDay = d.routes.reduce((s, r) => s + (r.timeline.km ?? 0), 0);
  const carried = d.orders.filter((o) => o.postponed_from !== undefined).length;
  const g = d.solver.gap;
  return (
    <div className="stats">
      <Stat label="Commandes" value={`${d.orders.length}`} sub={carried ? `dont ${carried} reportée(s) d'un jour précédent` : "du jour"} />
      <Stat label="Livrées" value={`${served}`} sub={d.postponed.length ? `${d.postponed.length} reportée(s)` : "aucun report"} warn={d.postponed.length > 0} />
      <Stat label="Chauffeurs" value={`${d.routes.length} / ${d.drivers.length}`} sub="sur la route / disponibles" />
      <Stat label="Distance" value={km(kmDay)} />
      <Stat label="Coût du jour" value={eur(d.check.objective)} sub={d.check.postponement_cost ? `dont reports ${eur(d.check.postponement_cost)}` : undefined} />
      <Stat label="Vérificateur" value={d.check.ok ? "conforme" : `${d.check.violations.length} infraction(s)`} bad={!d.check.ok} good={d.check.ok}
        sub={`${b.label ?? b.solver}${g !== null ? ` · écart ${(g * 100).toFixed(1)} %` : ""}`} />
    </div>
  );
}

export function Stat({ label, value, sub, warn, bad, good }: { label: string; value: string; sub?: string; warn?: boolean; bad?: boolean; good?: boolean }) {
  return (
    <div className={`stat${warn ? " warn" : ""}${bad ? " bad" : ""}${good ? " good" : ""}`}>
      <div className="stat-label">{label}</div>
      <div className="stat-value">{value}</div>
      {sub && <div className="stat-sub">{sub}</div>}
    </div>
  );
}

function DayPanel({ b, d, onSelectOrder }: { b: Bundle; d: Day; onSelectOrder: (o: string) => void }) {
  const cust = customerMap(b);
  return (
    <div className="stack tight">
      <h3>Tournées</h3>
      <ul className="routes">
        {d.routes.map((r) => {
          const facts = d.check.drivers.find((f) => f.driver_id === r.driver_id);
          return (
            <li key={r.driver_id}>
              <div className="route-head">
                <span className="dot" style={{ background: driverColor(b, r.driver_id) }} />
                <b>{driverName(b, r.driver_id)}</b>
                <span className="muted small">
                  {r.truck_id} · {r.timeline.pallets} pal. · {hhmm(r.departure)}–{hhmm(r.return_time)} · {facts ? eur(facts.cost) : ""}
                </span>
              </div>
              <div className="stops">
                {r.order_ids.map((o, i) => {
                  const c = cust.get(d.orders.find((x) => x.id === o)?.customer_id ?? "");
                  const br = r.breaks.find((x) => x.after === o);
                  return (
                    <span key={o}>
                      <button className="stop" onClick={() => onSelectOrder(o)} title={customerName(c)}>
                        {i + 1}. {customerName(c)}
                        {c?.needs_tail_lift ? " ⬍" : ""}
                      </button>
                      {br && <span className="brk">pause {br.minutes}′</span>}
                    </span>
                  );
                })}
              </div>
            </li>
          );
        })}
      </ul>
      {d.postponed.length > 0 && (
        <>
          <h3>Reportées</h3>
          <ul className="plain">
            {d.postponed.map((o) => {
              const ord = d.orders.find((x) => x.id === o);
              return (
                <li key={o}>
                  <button className="stop" onClick={() => onSelectOrder(o)}>
                    {customerName(cust.get(ord?.customer_id ?? ""))}
                  </button>{" "}
                  <span className="muted small">
                    pénalité {eur(ord?.penalty ?? 0)}
                    {ord?.postponed_from !== undefined ? ` · due depuis ${DAYS[ord.postponed_from].toLowerCase()}` : ""}
                  </span>
                </li>
              );
            })}
          </ul>
        </>
      )}
      <h3>Verdict du jour</h3>
      <ViolationList violations={d.check.violations} bundle={b} day={d} />
    </div>
  );
}

function OrderPanel({ b, d, orderId, onClose }: { b: Bundle; d: Day; orderId: string; onClose: () => void }) {
  const o = d.orders.find((x) => x.id === orderId);
  const c = b.instance.customers.find((x) => x.id === o?.customer_id);
  const r = d.routes.find((x) => x.order_ids.includes(orderId));
  const i = r ? r.order_ids.indexOf(orderId) : -1;
  if (!o || !c) return null;
  return (
    <div className="stack tight">
      <div className="panel-head">
        <h3>{customerName(c)}</h3>
        <button className="link" onClick={onClose}>
          ← tournées
        </button>
      </div>
      <dl className="facts">
        <dt>Type</dt>
        <dd>{TYPE_LABEL[c.type] ?? c.type}</dd>
        <dt>Commande</dt>
        <dd>
          {o.id} · {o.pallets} palette(s)
        </dd>
        <dt>Créneau</dt>
        <dd>
          {hhmm(c.window_start)} – {hhmm(c.window_end)} (début de livraison)
        </dd>
        <dt>Hayon</dt>
        <dd>{c.needs_tail_lift ? "obligatoire" : "non"}</dd>
        <dt>Durée prévue</dt>
        <dd>{dur(o.service_planned)}{o.service_mu !== undefined && o.service_mu !== o.service_planned ? ` (règle : ${dur(o.service_mu)})` : ""}</dd>
        {o.service_true !== undefined && (
          <>
            <dt>Durée réelle</dt>
            <dd>{dur(o.service_true)}</dd>
          </>
        )}
        {o.postponed_from !== undefined && (
          <>
            <dt>Due depuis</dt>
            <dd>{DAYS[o.postponed_from]}</dd>
          </>
        )}
        <dt>{r ? "Livrée par" : "Statut"}</dt>
        <dd>
          {r ? (
            <>
              {driverName(b, r.driver_id)} ({r.truck_id}), arrêt {i + 1}
              <br />
              arrivée {hhmm(r.arrivals[i])}, livraison {hhmm(r.service_starts[i])}
              {r.service_starts[i] > r.arrivals[i] ? ` (attente ${dur(r.service_starts[i] - r.arrivals[i])})` : ""}
            </>
          ) : d.postponed.includes(orderId) ? (
            <>Reportée — pénalité {eur(o.penalty)}</>
          ) : (
            "—"
          )}
        </dd>
      </dl>
    </div>
  );
}
