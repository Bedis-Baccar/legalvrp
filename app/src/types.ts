// Shapes of the files written by scripts/build_demo.py and the C++ viz module (src/legalvrp/viz).

export type SegmentKind = "idle" | "prep" | "drive" | "wait" | "service" | "break" | "close";

export interface Segment {
  kind: SegmentKind;
  start: number;
  end: number;
  order_id?: string;
  drive_since_break: number;
  work_since_break: number;
}

export interface Timeline {
  segments: Segment[];
  duty_start: number;
  duty_end: number;
  driving: number;
  work: number;
  breaks: number;
  km?: number;
  pallets?: number;
}

export interface Violation {
  rule: string;
  driver_id: string;
  order_id: string;
  amount: number;
  day: number;
}

export interface Outcome {
  late: boolean;
  illegal: boolean;
  late_stops: number;
  rules: string[];
  max_delay: number;
  overrun: number;
}

export interface Route {
  driver_id: string;
  truck_id: string;
  order_ids: string[];
  breaks: { after: string; minutes: number }[];
  departure: number;
  arrivals: number[];
  service_starts: number[];
  return_time: number;
  timeline: Timeline;
  outcome?: Outcome;
}

export interface DriverFacts {
  driver_id: string;
  used: boolean;
  service_minutes: number;
  driving_minutes: number;
  km: number;
  cost: number;
}

export interface DayCheck {
  ok: boolean;
  violations: Violation[];
  objective: number;
  postponement_cost: number;
  drivers: DriverFacts[];
}

export interface DayOrder {
  id: string;
  customer_id: string;
  pallets: number;
  service_planned: number;
  service_mu?: number;
  service_true?: number;
  penalty: number;
  postponed_from?: number;
}

export interface Day {
  day: number;
  orders: DayOrder[];
  drivers: string[];
  states: { driver_id: string; service_minutes_week: number; driving_minutes_week: number }[];
  routes: Route[];
  postponed: string[];
  check: DayCheck;
  solver: { status: string; runtime_s: number; objective: number; gap: number | null; best_bound: number | null };
  realised?: { routes: Route[]; check: DayCheck };
}

export interface Customer {
  id: string;
  name: string;
  display_name?: string;
  type: "grocery" | "restaurant" | "site";
  x_km: number;
  y_km: number;
  lat?: number;
  lon?: number;
  window_start: number;
  window_end: number;
  needs_tail_lift: boolean;
}

export interface Driver {
  id: string;
  display_name?: string;
  contract_class: string;
  truck_id: string;
  shift_start: number;
  shift_end_max: number;
  available_days: number[];
}

export interface Contract {
  name: string;
  weekly_threshold: number;
  weekly_service_max: number;
  cost_per_min_regular: number;
  cost_per_min_extra: number;
  fixed_cost_if_used: number;
}

export interface Rules {
  drive_before_break: number;
  work_before_break: number;
  break_length: number;
  daily_drive_max: number;
  daily_service_max: number;
  depot_prep: number;
  depot_close: number;
  earliest_duty_start: number;
  latest_duty_end: number;
  allow_short_break: boolean;
  allow_split_break: boolean;
  [k: string]: number | boolean;
}

export interface Instance {
  name: string;
  seed: number;
  days: number;
  depot: { id: string; x_km: number; y_km: number; lat?: number; lon?: number; name?: string };
  customers: Customer[];
  drivers: Driver[];
  trucks: { id: string; capacity_pallets: number; has_tail_lift: boolean }[];
  rules: Rules;
  contracts: Contract[];
  costs: { cost_per_km: number; postpone_penalty: number; postpone_escalation: number; unserved_end_penalty: number };
  certified: boolean;
}

export interface WeekKpis {
  cost_total: number;
  km: number;
  served: number;
  postponement_decisions: number;
  unserved_end: number;
  unserved_order_ids: string[];
  on_time_rate: number;
  hours_gini: number;
  extra_minutes_total: number;
  week_checker_ok: boolean;
  violations: number;
  days: { day: number; orders: number; served: number; postponed: number; drivers_used: number; km: number; cost: number; postponement_cost: number; service_minutes: Record<string, number> }[];
  drivers: { driver_id: string; contract: string; service_hours_week: number; driving_hours_week: number; threshold_hours: number; extra_minutes: number; days_worked: number }[];
}

export interface Bundle {
  schema: number;
  run: string;
  solver: string;
  label?: string;
  options: Record<string, unknown>;
  instance: Instance;
  days: Day[];
  kpis: WeekKpis;
  realised_kpis?: WeekKpis;
  week_check: { ok: boolean; violations: Violation[]; unserved_order_ids: string[] };
  geo?: { anchor: { lat: number; lon: number; label?: string }; synthetic: boolean };
}

export interface RunEntry {
  id: string;
  label: string;
  note?: string;
  solver: string;
  file?: string;
  cost?: number;
  checker_ok?: boolean;
  unavailable?: string;
}

export interface Focus {
  run?: string;
  day?: number;
  driver?: string;
  highlight?: string;
  text?: string;
}

export type View = "week" | "day" | "real" | "drivers" | "compare" | "report" | "gallery";

export interface ScenarioEntry {
  id: string;
  order: number;
  title: string;
  category: string;
  summary: string;
  story: string;
  view: View;
  focus: Focus[];
  compare: string[];
  runs: RunEntry[];
  gallery?: { file: string; featured?: string[] };
  report?: { kind: "lookahead"; source: string; rows: { seed: number; myopic: number; LA2: number; clairvoyant: number }[] };
}

export interface Index {
  schema: number;
  title: string;
  subtitle: string;
  disclaimer: string;
  geo: { lat: number; lon: number; depot_name?: string; label?: string };
  built: string;
  gurobi: boolean;
  scenarios: ScenarioEntry[];
}

export interface GalleryItem {
  name: string;
  expect: string[];
  got: string[];
  matches: boolean;
  violations: Violation[];
  stops: { id: string; window_start: number; window_end: number; service: number; pallets: number; needs_tail_lift: boolean }[];
  legs: number[];
  route: Route;
  timeline: Timeline | null;
  shift: { start: number; end_max: number };
  truck: { id: string; capacity_pallets: number; has_tail_lift: boolean };
  state: { driver_id: string; service_minutes_week: number; driving_minutes_week: number };
}

export interface Gallery {
  schema: number;
  description: string;
  rules: Rules;
  items: GalleryItem[];
}
