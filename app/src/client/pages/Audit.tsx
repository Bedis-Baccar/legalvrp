// The controller's side: every duty and every order, value against limit, and the checker's verdict.
import type { Ctx } from "../ClientApp";
import type { Bundle, Day, Route } from "../../types";
import { CONTRACT_LABEL, DAYS, dur, hhmm, RULES } from "../../format";
import { customerName, driverColor, driverName, routeRows } from "../../util";
import Gantt, { Legend } from "../../components/Gantt";
import { ViolationList } from "../../components/Verdict";
import { controlStatus, dutyControls, dutyViolations, margin, stopControls, TIGHT, type Control } from "../model";

function fmt(c: Control, v: number) {
  return c.unit === "time" ? hhmm(v) : c.unit === "pal" ? `${v} pal.` : dur(v);
}

function routesOf(d: Day, real: boolean): Route[] {
  return real ? d.realised?.routes ?? [] : d.routes;
}

function cellStatus(b: Bundle, d: Day, r: Route | undefined, real: boolean): "off" | "ok" | "tight" | "bad" | "late" {
  if (!r) return "off";
  if (dutyViolations(d, r.driver_id, real).some((v) => v.rule !== "time_window")) return "bad";
  if (dutyViolations(d, r.driver_id, real).length) return "late";
  const st = dutyControls(b, d, r).map(controlStatus);
  const windowTight = stopControls(b, d, r).some((s) => s.slack < TIGHT);
  return st.includes("bad") ? "bad" : st.includes("tight") || windowTight ? "tight" : "ok";
}

const CELL_LABEL = { off: "repos", ok: "conforme", tight: "conforme, marge < 15 min (temps ou créneau)", late: "créneau manqué", bad: "infraction" };

export default function Audit({ ctx }: { ctx: Ctx }) {
  const { bundle: b, nav, go } = ctx;
  const real = !!nav.real && b.days.some((d) => d.realised);
  const duties = b.days.flatMap((d) => routesOf(d, real).map((r) => ({ d, r })));
  const viol = b.days.flatMap((d) => (real ? d.realised?.check.violations ?? [] : d.check.violations).map((v) => ({ ...v, day: d.day })));
  const stops = duties.reduce((s, x) => s + x.r.order_ids.length, 0);
  // Families of controls: number of checks and the smallest margin over the week.
  const fam = new Map<string, { label: string; source: string; n: number; min: number; c: Control }>();
  for (const { d, r } of duties) {
    for (const c of dutyControls(b, d, r)) {
      const m = margin(c);
      const f = fam.get(c.key);
      if (!f) fam.set(c.key, { label: c.key === "capacity" ? "Chargement des camions" : c.label, source: c.source, n: 1, min: m, c });
      else {
        f.n += 1;
        f.min = Math.min(f.min, m);
      }
    }
  }
  const winSlack = duties.flatMap(({ d, r }) => stopControls(b, d, r).map((s) => s.slack));
  const tl = duties.flatMap(({ d, r }) => stopControls(b, d, r)).filter((s) => s.customer.needs_tail_lift);
  const selD = b.days.find((d) => d.day === nav.day);
  const selR = selD && nav.driver ? routesOf(selD, real).find((r) => r.driver_id === nav.driver) : undefined;

  return (
    <div className="stack">
      <div className="toolbar">
        <div className="seg-control">
          <button className={!real ? "on" : ""} onClick={() => go({ real: false })}>Plan tel que planifié</button>
          <button className={real ? "on" : ""} onClick={() => go({ real: true })} disabled={!b.days.some((d) => d.realised)}>Exécution simulée (vrais temps)</button>
        </div>
        <span className="muted small">
          {real ? "Mêmes départs, même ordre, mêmes pauses ; durées de livraison réelles tirées par le générateur." : "Le plan tel qu'il est remis aux chauffeurs."}
        </span>
      </div>

      <section className={`card c-verdict ${viol.length ? "is-ko" : "is-ok"}`}>
        <div className="c-verdict-main">
          <div className="c-verdict-icon">{viol.length ? "!" : "✓"}</div>
          <div>
            <h2>{viol.length ? `${viol.length} infraction(s) relevée(s)` : "Aucune infraction"}</h2>
            <div className="muted">
              Vérificateur indépendant : {duties.length} tournées, {stops} livraisons contrôlées sur la semaine
              {real ? " (exécution simulée)" : ""}. Il est écrit à partir des textes et ne partage aucun code avec les planificateurs.
            </div>
          </div>
        </div>
        <div className="table-wrap">
          <table className="kpi c-fam">
            <thead>
              <tr>
                <th>Contrôle</th>
                <th>Référence</th>
                <th>Contrôles</th>
                <th>Marge minimale sur la semaine</th>
              </tr>
            </thead>
            <tbody>
              {[...fam.values()].map((f) => (
                <tr key={f.label}>
                  <td>{f.label}</td>
                  <td className="muted small">{f.source}</td>
                  <td>{f.n}</td>
                  <td className={f.min < 0 ? "bad" : f.min < TIGHT && f.c.kind === "max" && f.c.unit !== "pal" ? "warn" : ""}>{f.c.unit === "pal" ? `${f.min} pal.` : dur(f.min)}</td>
                </tr>
              ))}
              <tr>
                <td>Créneaux des clients</td>
                <td className="muted small">Client</td>
                <td>{winSlack.length}</td>
                <td className={Math.min(...winSlack) < 0 ? "bad" : Math.min(...winSlack) < 15 ? "warn" : ""}>{winSlack.length ? dur(Math.min(...winSlack)) : "—"}</td>
              </tr>
              <tr>
                <td>Hayon (clients qui l'exigent)</td>
                <td className="muted small">Client</td>
                <td>{tl.length}</td>
                <td>{tl.every((s) => s.tailLiftOk) ? "tous servis par un camion équipé" : <span className="bad">camion non équipé</span>}</td>
              </tr>
            </tbody>
          </table>
        </div>
        {viol.length > 0 && (
          <div className="c-violist">
            <ViolationList violations={viol} bundle={b} />
          </div>
        )}
      </section>

      <section className="card">
        <h3>Tournées par chauffeur et par jour</h3>
        <div className="legend">
          {(["ok", "tight", "late", "bad", "off"] as const).map((k) => (
            <span key={k}><i className={`c-cellkey ${k}`} /> {CELL_LABEL[k]}</span>
          ))}
        </div>
        <div className="table-wrap">
          <table className="c-matrix">
            <thead>
              <tr>
                <th />
                {b.days.map((d) => <th key={d.day}>{DAYS[d.day]}</th>)}
              </tr>
            </thead>
            <tbody>
              {b.instance.drivers.map((drv) => (
                <tr key={drv.id}>
                  <th>
                    <span className="dot" style={{ background: driverColor(b, drv.id) }} />
                    {driverName(b, drv.id)}
                    <div className="muted small">{CONTRACT_LABEL[drv.contract_class]}</div>
                  </th>
                  {b.days.map((d) => {
                    const r = routesOf(d, real).find((x) => x.driver_id === drv.id);
                    const st = cellStatus(b, d, r, real);
                    const on = nav.driver === drv.id && nav.day === d.day;
                    return (
                      <td key={d.day}>
                        <button className={`c-cell ${st}${on ? " on" : ""}`} disabled={!r} onClick={() => go({ driver: drv.id, day: d.day })}>
                          {r ? (
                            <>
                              <b>{r.order_ids.length}</b> arrêts
                              <span>{hhmm(r.timeline.duty_start)}–{hhmm(r.timeline.duty_end)}</span>
                            </>
                          ) : (
                            "—"
                          )}
                        </button>
                      </td>
                    );
                  })}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>

      {selD && selR ? <DutyAudit b={b} d={selD} r={selR} real={real} /> : <div className="notice">Choisissez une tournée dans le tableau pour voir le détail de ses contrôles, de ses tâches et de ses commandes.</div>}
    </div>
  );
}

function DutyAudit({ b, d, r, real }: { b: Bundle; d: Day; r: Route; real: boolean }) {
  const cs = dutyControls(b, d, r);
  const ss = stopControls(b, d, r);
  const v = dutyViolations(d, r.driver_id, real);
  const planned = real ? d.routes.find((x) => x.driver_id === r.driver_id) : undefined;
  return (
    <section className="card c-print">
      <div className="panel-head">
        <h3>
          <span className="dot" style={{ background: driverColor(b, r.driver_id) }} />
          {driverName(b, r.driver_id)} — {DAYS[d.day]} {real ? "(exécution simulée)" : ""}
        </h3>
        <button className="btn no-print" onClick={() => window.print()}>Imprimer le contrôle</button>
      </div>
      <Legend />
      <Gantt rows={routeRows(b, d, [r], planned ? [planned] : undefined)} limits={{ drive: b.instance.rules.drive_before_break, work: b.instance.rules.work_before_break }} />
      <div className="c-grid2">
        <div>
          <h3>Règles de temps et du véhicule</h3>
          <table className="kpi c-controls">
            <thead>
              <tr>
                <th>Contrôle</th>
                <th>Valeur</th>
                <th>Limite</th>
                <th>Marge</th>
                <th />
              </tr>
            </thead>
            <tbody>
              {cs.map((c) => {
                const st = controlStatus(c);
                return (
                  <tr key={c.key}>
                    <td>
                      {c.label}
                      <div className="muted small">{c.source}{c.detail ? ` · ${c.detail}` : ""}</div>
                    </td>
                    <td>{fmt(c, c.value)}</td>
                    <td>{c.kind === "max" ? "≤ " : "≥ "}{fmt(c, c.limit)}</td>
                    <td>{c.unit === "pal" ? `${margin(c)} pal.` : dur(margin(c))}</td>
                    <td><span className={`c-st ${st}`}>{st === "ok" ? "✓" : st === "tight" ? "≈" : "✗"}</span></td>
                  </tr>
                );
              })}
            </tbody>
          </table>
          <p className="muted small">Valeurs lues sur le planning de la tournée (même calcul que le vérificateur). « ≈ » : moins de 15 min avant une limite qu'un retard ferait dépasser.</p>
        </div>
        <div>
          <h3>Commandes de la tournée</h3>
          <table className="kpi c-controls">
            <thead>
              <tr>
                <th>#</th>
                <th>Client</th>
                <th>Créneau</th>
                <th>Livraison</th>
                <th>Marge</th>
                <th>Hayon</th>
                <th>À bord après</th>
              </tr>
            </thead>
            <tbody>
              {ss.map((s) => (
                <tr key={s.orderId}>
                  <td>{s.stop + 1}</td>
                  <td>{customerName(s.customer)}<div className="muted small mono">{s.orderId} · {s.pallets} pal.</div></td>
                  <td>{hhmm(s.windowStart)}–{hhmm(s.windowEnd)}</td>
                  <td>{hhmm(s.start)}{s.start > s.arrival ? <div className="muted small">arrivée {hhmm(s.arrival)}</div> : null}</td>
                  <td className={s.slack < 0 ? "bad" : s.slack < 15 ? "warn" : ""}>{dur(s.slack)}</td>
                  <td>{s.customer.needs_tail_lift ? (s.tailLiftOk ? "✓ requis" : <span className="bad">✗</span>) : "—"}</td>
                  <td>{s.loadAfter} pal.</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </div>
      <h3>Verdict du vérificateur</h3>
      <ViolationList violations={v} bundle={b} day={d} />
      {v.length > 0 && (
        <p className="small muted">
          Règles en cause : {[...new Set(v.map((x) => x.rule))].map((k) => `${RULES[k]?.title ?? k} (${RULES[k]?.source ?? ""})`).join(" ; ")}.
        </p>
      )}
    </section>
  );
}
