// Derived views of a run bundle for the client mock-up: the week's orders with their fate, the
// controls of a duty (value, limit, margin), cost breakdown. Verdicts always come from the checker
// (bundle.days[].check, realised.check); the controls below are margins read off the timeline,
// which is the checker's own walk of the route (src/legalvrp/viz/timeline.hpp).
import type { Bundle, Customer, Day, DayOrder, Route, Violation } from "../types";

export interface OrderFate {
  id: string;
  customer: Customer;
  firstDay: number;                 // day it was first due
  pallets: number;
  servicePlanned: number;
  serviceTrue?: number;
  postponedOn: number[];            // days on which it was postponed
  penalties: number;                // sum of the penalties paid while postponed
  served?: { day: number; driver: string; truck: string; stop: number; arrival: number; start: number; end: number; route: Route };
  realisedStart?: number;           // service start when executed with the true times
  status: "served" | "late_served" | "unserved";
}

export function orderFates(b: Bundle): OrderFate[] {
  const cust = new Map(b.instance.customers.map((c) => [c.id, c]));
  const fates = new Map<string, OrderFate>();
  const get = (o: DayOrder, day: number): OrderFate => {
    let f = fates.get(o.id);
    if (!f) {
      f = {
        id: o.id,
        customer: cust.get(o.customer_id)!,
        firstDay: o.postponed_from ?? day,
        pallets: o.pallets,
        servicePlanned: o.service_planned,
        serviceTrue: o.service_true,
        postponedOn: [],
        penalties: 0,
        status: "unserved",
      };
      fates.set(o.id, f);
    }
    return f;
  };
  for (const d of b.days) {
    for (const o of d.orders) {
      const f = get(o, d.day);
      if (d.postponed.includes(o.id)) {
        f.postponedOn.push(d.day);
        f.penalties += o.penalty;
      }
    }
    for (const r of d.routes) {
      r.order_ids.forEach((oid, i) => {
        const f = fates.get(oid);
        if (!f) return;
        const o = d.orders.find((x) => x.id === oid)!;
        f.served = { day: d.day, driver: r.driver_id, truck: r.truck_id, stop: i, arrival: r.arrivals[i], start: r.service_starts[i], end: r.service_starts[i] + o.service_planned, route: r };
        f.status = f.postponedOn.length ? "late_served" : "served";
        const rr = d.realised?.routes.find((x) => x.driver_id === r.driver_id);
        if (rr) f.realisedStart = rr.service_starts[i];
      });
    }
  }
  return [...fates.values()].sort((a, b) => a.firstDay - b.firstDay || a.id.localeCompare(b.id));
}

export interface Control {
  key: string;
  label: string;
  value: number;
  limit: number;
  kind: "max" | "min";             // value must stay <= limit (max) or >= limit (min)
  unit: "min" | "pal" | "time";
  source: string;
  detail?: string;
}

export const margin = (c: Control) => (c.kind === "max" ? c.limit - c.value : c.value - c.limit);
export const TIGHT = 15;           // minutes: a margin below this is shown as tight

// "Tight" only where a delay can break the limit: upper limits on time (driving and work blocks,
// daily and weekly totals, end of duty). A departure at the earliest allowed minute, breaks exactly
// as long as required or a full truck are not at risk from a late delivery.
export function controlStatus(c: Control): "ok" | "tight" | "bad" {
  const m = margin(c);
  if (m < 0) return "bad";
  if (c.kind === "max" && c.unit !== "pal" && m < TIGHT) return "tight";
  return "ok";
}

// Controls of one duty, from its timeline and the instance (rules, contracts, trucks, windows).
export function dutyControls(b: Bundle, day: Day, r: Route): Control[] {
  const rules = b.instance.rules;
  const t = r.timeline;
  const drv = b.instance.drivers.find((d) => d.id === r.driver_id)!;
  const contract = b.instance.contracts.find((c) => c.name === drv.contract_class)!;
  const truck = b.instance.trucks.find((x) => x.id === r.truck_id)!;
  const state = day.states.find((s) => s.driver_id === r.driver_id);
  const maxDrive = Math.max(0, ...t.segments.map((s) => s.drive_since_break));
  const maxWork = Math.max(0, ...t.segments.map((s) => s.work_since_break));
  const breakNeed = t.work > (rules.short_break_work_max as number) ? rules.break_length : t.work > rules.work_before_break ? (rules.short_break_length as number) : 0;
  const pallets = t.pallets ?? 0;
  const cs: Control[] = [
    { key: "drive_block", label: "Conduite continue (entre deux pauses)", value: maxDrive, limit: rules.drive_before_break, kind: "max", unit: "min", source: "Règl. 561/2006 art. 7" },
    { key: "work_block", label: "Travail continu (entre deux pauses)", value: maxWork, limit: rules.work_before_break, kind: "max", unit: "min", source: "Dir. 2002/15 art. 5 ; L3312-2" },
    { key: "breaks", label: "Pauses cumulées", value: t.breaks, limit: breakNeed, kind: "min", unit: "min", source: "Dir. 2002/15 art. 5 ; L3312-2", detail: breakNeed ? `${breakNeed} min exigées pour ${Math.floor(t.work / 60)} h ${t.work % 60} de travail` : "aucune pause exigée (≤ 6 h de travail)" },
    { key: "daily_drive", label: "Conduite de la journée", value: t.driving, limit: rules.daily_drive_max, kind: "max", unit: "min", source: "Règl. 561/2006 art. 6" },
    { key: "daily_service", label: "Temps de service de la journée", value: t.work, limit: rules.daily_service_max, kind: "max", unit: "min", source: "C. transports D3312-51" },
    { key: "start", label: "Départ après prise de poste + préparation", value: r.departure, limit: drv.shift_start + rules.depot_prep, kind: "min", unit: "time", source: "Contrat" },
    { key: "end", label: "Fin de service", value: t.duty_end, limit: Math.min(drv.shift_end_max, rules.latest_duty_end), kind: "max", unit: "time", source: "Contrat ; pas de travail de nuit" },
    { key: "capacity", label: `Chargement (${truck.id})`, value: pallets, limit: truck.capacity_pallets, kind: "max", unit: "pal", source: "Camion" },
  ];
  if (state) {
    cs.push({ key: "week_service", label: "Temps de service de la semaine", value: state.service_minutes_week + t.work, limit: contract.weekly_service_max, kind: "max", unit: "min", source: "Contrat", detail: `seuil d'heures sup. ${Math.round(contract.weekly_threshold / 60)} h` });
    cs.push({ key: "week_drive", label: "Conduite de la semaine", value: state.driving_minutes_week + t.driving, limit: (rules.weekly_drive_max as number) ?? 3360, kind: "max", unit: "min", source: "Règl. 561/2006 art. 6" });
  }
  return cs;
}

export interface StopControl {
  orderId: string;
  customer: Customer;
  stop: number;
  arrival: number;
  start: number;
  windowStart: number;
  windowEnd: number;
  slack: number;                    // window end - service start
  tailLiftOk: boolean;
  pallets: number;
  loadAfter: number;                // pallets still on board after this stop
}

export function stopControls(b: Bundle, day: Day, r: Route): StopControl[] {
  const cust = new Map(b.instance.customers.map((c) => [c.id, c]));
  const truck = b.instance.trucks.find((x) => x.id === r.truck_id)!;
  let load = r.timeline.pallets ?? 0;
  return r.order_ids.map((oid, i) => {
    const o = day.orders.find((x) => x.id === oid)!;
    const c = cust.get(o.customer_id)!;
    load -= o.pallets;
    return {
      orderId: oid,
      customer: c,
      stop: i,
      arrival: r.arrivals[i],
      start: r.service_starts[i],
      windowStart: c.window_start,
      windowEnd: c.window_end,
      slack: c.window_end - r.service_starts[i],
      tailLiftOk: !c.needs_tail_lift || truck.has_tail_lift,
      pallets: o.pallets,
      loadAfter: load,
    };
  });
}

export function dutyViolations(day: Day, driverId: string, realised = false): Violation[] {
  const check = realised ? day.realised?.check : day.check;
  return (check?.violations ?? []).filter((v) => v.driver_id === driverId);
}

export interface CostParts {
  km: number;
  staff: number;          // regular + overtime + temp agency
  penalties: number;
  total: number;
}

export function costParts(b: Bundle): CostParts {
  const km = b.kpis.km * b.instance.costs.cost_per_km;
  const penalties = b.days.reduce((s, d) => s + d.check.postponement_cost, 0);
  const total = b.kpis.cost_total;
  return { km, penalties, staff: Math.max(0, total - km - penalties), total };
}

export function realisedTally(b: Bundle, day?: number) {
  const days = day === undefined ? b.days : b.days.filter((d) => d.day === day);
  const rs = days.flatMap((d) => d.realised?.routes ?? []);
  return {
    duties: rs.length,
    late: rs.filter((r) => r.outcome?.late).length,
    illegal: rs.filter((r) => r.outcome?.illegal).length,
    either: rs.filter((r) => r.outcome?.late || r.outcome?.illegal).length,
  };
}

export function fillRate(b: Bundle): number {
  const caps = new Map(b.instance.trucks.map((t) => [t.id, t.capacity_pallets]));
  let load = 0;
  let cap = 0;
  for (const d of b.days) {
    for (const r of d.routes) {
      load += r.timeline.pallets ?? 0;
      cap += caps.get(r.truck_id) ?? 0;
    }
  }
  return cap ? load / cap : 0;
}
