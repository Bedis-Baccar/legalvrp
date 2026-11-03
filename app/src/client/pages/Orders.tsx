import { useMemo, useState } from "react";
import type { Ctx } from "../ClientApp";
import { DAYS, dur, eur, hhmm, TYPE_LABEL } from "../../format";
import { customerName, driverColor, driverName } from "../../util";
import { orderFates, type OrderFate } from "../model";

type Status = "all" | "served" | "late_served" | "unserved";
const STATUS_LABEL: Record<Exclude<Status, "all">, string> = { served: "Livrée à la date", late_served: "Livrée après report", unserved: "Non livrée" };

function csv(rows: OrderFate[], b: Ctx["bundle"]): string {
  const head = ["commande", "jour_prevu", "client", "type", "palettes", "hayon", "creneau_debut", "creneau_fin", "statut", "jour_livraison", "chauffeur", "camion", "arrivee", "debut_livraison", "marge_creneau_min", "duree_prevue_min", "duree_reelle_min", "reports", "penalites_eur"];
  const lines = rows.map((f) => [
    f.id, DAYS[f.firstDay], customerName(f.customer), f.customer.type, f.pallets, f.customer.needs_tail_lift ? "oui" : "non",
    hhmm(f.customer.window_start), hhmm(f.customer.window_end), STATUS_LABEL[f.status],
    f.served ? DAYS[f.served.day] : "", f.served ? driverName(b, f.served.driver) : "", f.served?.truck ?? "",
    f.served ? hhmm(f.served.arrival) : "", f.served ? hhmm(f.served.start) : "", f.served ? f.customer.window_end - f.served.start : "",
    f.servicePlanned, f.serviceTrue ?? "", f.postponedOn.length, f.penalties,
  ]);
  return [head, ...lines].map((l) => l.map((v) => `"${String(v).replace(/"/g, '""')}"`).join(";")).join("\n");
}

export default function Orders({ ctx }: { ctx: Ctx }) {
  const { bundle: b, nav, go, run } = ctx;
  const fates = useMemo(() => orderFates(b), [b]);
  const [q, setQ] = useState("");
  const [day, setDay] = useState<number | "all">("all");
  const [status, setStatus] = useState<Status>("all");
  const [type, setType] = useState("all");
  const [driver, setDriver] = useState("all");
  const [tail, setTail] = useState(false);
  const rows = fates.filter(
    (f) =>
      (day === "all" || f.firstDay === day) &&
      (status === "all" || f.status === status) &&
      (type === "all" || f.customer.type === type) &&
      (driver === "all" || f.served?.driver === driver) &&
      (!tail || f.customer.needs_tail_lift) &&
      (!q || `${f.id} ${customerName(f.customer)}`.toLowerCase().includes(q.toLowerCase())),
  );
  const sel = fates.find((f) => f.id === nav.order);
  const download = () => {
    const blob = new Blob(["﻿" + csv(rows, b)], { type: "text/csv;charset=utf-8" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = `commandes_${run.id}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
  };
  return (
    <div className={sel ? "c-split" : ""}>
      <div className="stack">
        <div className="c-filters">
          <input placeholder="Rechercher un client ou une commande…" value={q} onChange={(e) => setQ(e.target.value)} />
          <select value={day} onChange={(e) => setDay(e.target.value === "all" ? "all" : Number(e.target.value))}>
            <option value="all">Tous les jours</option>
            {b.days.map((d) => <option key={d.day} value={d.day}>{DAYS[d.day]}</option>)}
          </select>
          <select value={status} onChange={(e) => setStatus(e.target.value as Status)}>
            <option value="all">Tous les statuts</option>
            {Object.entries(STATUS_LABEL).map(([k, v]) => <option key={k} value={k}>{v}</option>)}
          </select>
          <select value={type} onChange={(e) => setType(e.target.value)}>
            <option value="all">Tous les types</option>
            {Object.entries(TYPE_LABEL).map(([k, v]) => <option key={k} value={k}>{v}</option>)}
          </select>
          <select value={driver} onChange={(e) => setDriver(e.target.value)}>
            <option value="all">Tous les chauffeurs</option>
            {b.instance.drivers.map((d) => <option key={d.id} value={d.id}>{driverName(b, d.id)}</option>)}
          </select>
          <label className="switch"><input type="checkbox" checked={tail} onChange={(e) => setTail(e.target.checked)} /> Hayon</label>
          <span className="muted small">{rows.length} / {fates.length}</span>
          <button className="btn" onClick={download}>Exporter (CSV)</button>
        </div>
        <section className="card c-tablecard">
          <div className="table-wrap c-scroll">
            <table className="kpi c-orders">
              <thead>
                <tr>
                  <th>Commande</th>
                  <th>Prévue</th>
                  <th>Client</th>
                  <th>Pal.</th>
                  <th>Créneau</th>
                  <th>Statut</th>
                  <th>Livrée par</th>
                  <th>Heure</th>
                  <th>Marge créneau</th>
                </tr>
              </thead>
              <tbody>
                {rows.map((f) => (
                  <tr key={f.id} className={f.id === nav.order ? "sel" : ""} onClick={() => go({ order: f.id })}>
                    <td className="mono">{f.id}</td>
                    <td>{DAYS[f.firstDay].slice(0, 3)}</td>
                    <td>
                      {customerName(f.customer)}
                      {f.customer.needs_tail_lift && <span className="tag">hayon</span>}
                    </td>
                    <td>{f.pallets}</td>
                    <td>{hhmm(f.customer.window_start)}–{hhmm(f.customer.window_end)}</td>
                    <td>
                      <span className={`badge ${f.status === "served" ? "ok" : f.status === "late_served" ? "warnb" : "bad"}`}>{STATUS_LABEL[f.status]}</span>
                    </td>
                    <td>{f.served ? <><span className="dot" style={{ background: driverColor(b, f.served.driver) }} />{driverName(b, f.served.driver)}</> : "—"}</td>
                    <td>{f.served ? `${DAYS[f.served.day].slice(0, 3)} ${hhmm(f.served.start)}` : "—"}</td>
                    <td>{f.served ? <Slack v={f.customer.window_end - f.served.start} /> : "—"}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      </div>
      {sel && <OrderCard ctx={ctx} f={sel} onClose={() => go({ order: undefined })} />}
    </div>
  );
}

function Slack({ v }: { v: number }) {
  return <span className={v < 0 ? "bad" : v < 15 ? "warn" : ""}>{dur(v)}</span>;
}

function OrderCard({ ctx, f, onClose }: { ctx: Ctx; f: OrderFate; onClose: () => void }) {
  const { bundle: b, go } = ctx;
  const c = f.customer;
  const s = f.served;
  const realDelay = s && f.realisedStart !== undefined ? f.realisedStart - s.start : undefined;
  // A small timeline of the day: the window, the planned and the realised service.
  const t0 = Math.min(c.window_start, s?.arrival ?? c.window_start) - 30;
  const t1 = Math.max(c.window_end, (f.realisedStart ?? 0) + (f.serviceTrue ?? 0), s?.end ?? 0) + 30;
  const x = (t: number) => `${(100 * (t - t0)) / (t1 - t0)}%`;
  const w = (a: number, z: number) => `${(100 * (z - a)) / (t1 - t0)}%`;
  return (
    <aside className="card c-drawer">
      <div className="panel-head">
        <h3>{customerName(c)}</h3>
        <button className="link" onClick={onClose}>✕</button>
      </div>
      <div className="muted small">{f.id} · {TYPE_LABEL[c.type]} · {f.pallets} palette(s){c.needs_tail_lift ? " · hayon obligatoire" : ""}</div>
      <div className="c-otl">
        <div className="win" style={{ left: x(c.window_start), width: w(c.window_start, c.window_end) }}>créneau</div>
        {s && <div className="plan" style={{ left: x(s.start), width: w(s.start, s.end) }} title="Livraison prévue" />}
        {s && f.realisedStart !== undefined && f.serviceTrue !== undefined && (
          <div className={`real ${f.realisedStart > c.window_end ? "late" : ""}`} style={{ left: x(f.realisedStart), width: w(f.realisedStart, f.realisedStart + f.serviceTrue) }} title="Livraison réelle (simulée)" />
        )}
        <div className="axis"><span>{hhmm(t0)}</span><span>{hhmm(t1)}</span></div>
      </div>
      <div className="legend small"><span><i className="win" /> créneau</span><span><i style={{ background: "var(--seg-service)" }} /> prévue</span><span><i style={{ background: "var(--c-real)" }} /> réelle (simulée)</span></div>
      <dl className="facts">
        <dt>Prévue le</dt><dd>{DAYS[f.firstDay]}</dd>
        <dt>Créneau</dt><dd>{hhmm(c.window_start)} – {hhmm(c.window_end)}</dd>
        <dt>Statut</dt><dd>{STATUS_LABEL[f.status]}{f.postponedOn.length ? ` (reportée ${f.postponedOn.map((d) => DAYS[d].toLowerCase()).join(", ")} — pénalités ${eur(f.penalties)})` : ""}</dd>
        {s && (
          <>
            <dt>Livrée</dt><dd>{DAYS[s.day]}, arrêt {s.stop + 1} de {s.route.order_ids.length}</dd>
            <dt>Chauffeur</dt><dd><a href="#" onClick={(e) => { e.preventDefault(); go({ page: "drivers", driver: s.driver, day: s.day, order: undefined }); }}>{driverName(b, s.driver)}</a> · {s.truck}</dd>
            <dt>Arrivée</dt><dd>{hhmm(s.arrival)}{s.start > s.arrival ? ` (attente ${dur(s.start - s.arrival)})` : ""}</dd>
            <dt>Livraison</dt><dd>{hhmm(s.start)} – {hhmm(s.end)}</dd>
            <dt>Marge sur le créneau</dt><dd><Slack v={c.window_end - s.start} /></dd>
          </>
        )}
        <dt>Durée prévue</dt><dd>{dur(f.servicePlanned)}</dd>
        {f.serviceTrue !== undefined && <><dt>Durée réelle (simulée)</dt><dd>{dur(f.serviceTrue)}</dd></>}
        {realDelay !== undefined && <><dt>Retard réel au début</dt><dd className={f.realisedStart! > c.window_end ? "bad" : realDelay > 0 ? "warn" : ""}>{realDelay > 0 ? `+${dur(realDelay)}` : "à l'heure"}{f.realisedStart! > c.window_end ? " — créneau manqué" : ""}</dd></>}
      </dl>
      {s && <button className="btn" onClick={() => go({ page: "audit", driver: s.driver, day: s.day, order: undefined })}>Contrôle de la tournée →</button>}
    </aside>
  );
}
