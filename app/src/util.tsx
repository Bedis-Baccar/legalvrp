import { Fragment, useEffect, useRef, useState, type ReactNode } from "react";
import type { Bundle, Customer, Day, Driver } from "./types";

// Driver colours: distinct hues, readable on light and dark backgrounds.
export const PALETTE = ["#2563eb", "#e8590c", "#0f9d58", "#c2255c", "#7048e8", "#0c8599", "#b08900", "#5c7cfa", "#d6336c", "#2b8a3e"];

export function driverColor(bundle: Bundle, driverId: string): string {
  const i = bundle.instance.drivers.findIndex((d) => d.id === driverId);
  return PALETTE[(i < 0 ? 0 : i) % PALETTE.length];
}

export function driverName(bundle: Bundle, driverId: string): string {
  const d = bundle.instance.drivers.find((x) => x.id === driverId);
  return d?.display_name ? `${d.display_name} (${d.id})` : driverId;
}

export function driverOf(bundle: Bundle, driverId: string): Driver | undefined {
  return bundle.instance.drivers.find((x) => x.id === driverId);
}

export function customerMap(bundle: Bundle): Map<string, Customer> {
  return new Map(bundle.instance.customers.map((c) => [c.id, c]));
}

export function customerName(c?: Customer): string {
  return c ? c.display_name ?? c.name : "?";
}

export function orderCustomer(bundle: Bundle, day: Day, orderId: string): Customer | undefined {
  const o = day.orders.find((x) => x.id === orderId);
  return o ? bundle.instance.customers.find((c) => c.id === o.customer_id) : undefined;
}

// Width of an element, kept up to date (charts draw at the real pixel width).
export function useWidth<T extends HTMLElement>(): [React.RefObject<T | null>, number] {
  const ref = useRef<T>(null);
  const [w, setW] = useState(800);
  useEffect(() => {
    if (!ref.current) return;
    const ro = new ResizeObserver((es) => setW(Math.max(320, Math.floor(es[0].contentRect.width))));
    ro.observe(ref.current);
    return () => ro.disconnect();
  }, []);
  return [ref, w];
}

// Minimal rich text for the scenario stories: paragraphs, "- " and "1. " lists, **bold**.
function inline(text: string): ReactNode[] {
  return text.split(/(\*\*[^*]+\*\*)/g).map((part, i) =>
    part.startsWith("**") && part.endsWith("**") ? <strong key={i}>{part.slice(2, -2)}</strong> : <Fragment key={i}>{part}</Fragment>,
  );
}

export function RichText({ text }: { text: string }) {
  const blocks = text.trim().split(/\n\s*\n/);
  return (
    <>
      {blocks.map((b, i) => {
        const lines = b.split("\n").filter((l) => l.trim());
        if (lines.every((l) => /^\s*- /.test(l))) return <ul key={i}>{lines.map((l, j) => <li key={j}>{inline(l.replace(/^\s*- /, ""))}</li>)}</ul>;
        if (lines.every((l) => /^\s*\d+\. /.test(l))) return <ol key={i}>{lines.map((l, j) => <li key={j}>{inline(l.replace(/^\s*\d+\. /, ""))}</li>)}</ol>;
        const head = lines.filter((l) => !/^\s*(- |\d+\. )/.test(l));
        const items = lines.filter((l) => /^\s*(- |\d+\. )/.test(l));
        if (items.length && head.length) {
          const numbered = /^\s*\d+\. /.test(items[0]);
          const lis = items.map((l, j) => <li key={j}>{inline(l.replace(/^\s*(- |\d+\. )/, ""))}</li>);
          return (
            <Fragment key={i}>
              <p>{inline(head.join(" "))}</p>
              {numbered ? <ol>{lis}</ol> : <ul>{lis}</ul>}
            </Fragment>
          );
        }
        return <p key={i}>{inline(lines.join(" "))}</p>;
      })}
    </>
  );
}

export function Loading({ error }: { error?: string }) {
  return <div className={error ? "notice error" : "notice"}>{error ? `Erreur de chargement : ${error}` : "Chargement…"}</div>;
}

import type { GanttRow } from "./components/Gantt";
import type { Route } from "./types";
import { CONTRACT_LABEL, dur } from "./format";

// Gantt rows of a day's routes, with the customers' windows and names.
export function routeRows(bundle: Bundle, day: Day, routes: Route[], planned?: Route[]): GanttRow[] {
  const cust = customerMap(bundle);
  const windows: Record<string, [number, number]> = {};
  const names: Record<string, string> = {};
  for (const o of day.orders) {
    const c = cust.get(o.customer_id);
    if (!c) continue;
    windows[o.id] = [c.window_start, c.window_end];
    names[o.id] = customerName(c);
  }
  return routes.map((r) => {
    const d = driverOf(bundle, r.driver_id);
    const status = r.outcome ? (r.outcome.illegal ? "illegal" : r.outcome.late ? "late" : "ok") : undefined;
    const sub = r.outcome
      ? r.outcome.illegal
        ? "illégale"
        : r.outcome.late
          ? `${r.outcome.late_stops} livraison(s) en retard`
          : "à l'heure"
      : `${CONTRACT_LABEL[d?.contract_class ?? ""] ?? d?.contract_class ?? ""} · ${r.truck_id} · ${dur(r.timeline.work)} trav.`;
    return {
      key: r.driver_id,
      label: driverName(bundle, r.driver_id),
      sublabel: sub,
      color: driverColor(bundle, r.driver_id),
      timeline: r.timeline,
      windows,
      names,
      status,
      shiftEnd: d?.shift_end_max,
      ghost: planned?.find((p) => p.driver_id === r.driver_id)?.timeline,
    };
  });
}
