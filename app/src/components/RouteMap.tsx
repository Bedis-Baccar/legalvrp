// Routes of a day on a map. Coordinates are synthetic (the generator's plane, anchored on the depot):
// lines are straight segments ("trajets schématiques"). The OpenStreetMap background needs internet;
// the schematic mode needs nothing.
import { useEffect, useRef } from "react";
import L from "leaflet";
import "leaflet/dist/leaflet.css";
import type { Bundle, Day, Route } from "../types";
import { customerName, driverColor, driverName } from "../util";
import { hhmm, TYPE_LABEL } from "../format";

interface Props {
  bundle: Bundle;
  day: Day;
  routes: Route[];
  tiles: boolean;
  highlight?: string;            // "tail_lift"
  selectedOrder?: string;
  selectedDriver?: string;
  onSelectOrder?: (orderId: string) => void;
}

export default function RouteMap({ bundle, day, routes, tiles, highlight, selectedOrder, selectedDriver, onSelectOrder }: Props) {
  const el = useRef<HTMLDivElement>(null);
  const map = useRef<L.Map | null>(null);
  const layer = useRef<L.LayerGroup | null>(null);
  const tileLayer = useRef<L.TileLayer | null>(null);
  const fitted = useRef<string>("");

  useEffect(() => {
    if (!el.current || map.current) return;
    map.current = L.map(el.current, { zoomControl: true, attributionControl: true, preferCanvas: false });
    layer.current = L.layerGroup().addTo(map.current);
    return () => {
      map.current?.remove();
      map.current = null;
    };
  }, []);

  useEffect(() => {
    const m = map.current;
    if (!m) return;
    if (tiles && !tileLayer.current) {
      tileLayer.current = L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png", {
        maxZoom: 18,
        attribution: '&copy; contributeurs <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a>',
      }).addTo(m);
    } else if (!tiles && tileLayer.current) {
      tileLayer.current.remove();
      tileLayer.current = null;
    }
  }, [tiles]);

  useEffect(() => {
    const m = map.current;
    const g = layer.current;
    if (!m || !g) return;
    g.clearLayers();
    const inst = bundle.instance;
    const depot = inst.depot;
    const P = (o: { lat?: number; lon?: number }) => L.latLng(o.lat ?? 0, o.lon ?? 0);
    const cust = new Map(inst.customers.map((c) => [c.id, c]));
    const orderById = new Map(day.orders.map((o) => [o.id, o]));
    const servedBy = new Map<string, Route>();
    routes.forEach((r) => r.order_ids.forEach((o) => servedBy.set(o, r)));
    const bounds = L.latLngBounds([P(depot)]);

    // Routes (straight legs, depot -> stops -> depot).
    for (const r of routes) {
      const pts = [P(depot), ...r.order_ids.map((o) => P(cust.get(orderById.get(o)!.customer_id)!)), P(depot)];
      const dim = selectedDriver && selectedDriver !== r.driver_id;
      L.polyline(pts, { color: driverColor(bundle, r.driver_id), weight: dim ? 2 : 4, opacity: dim ? 0.25 : 0.85 })
        .bindTooltip(driverName(bundle, r.driver_id), { sticky: true })
        .addTo(g);
      pts.forEach((p) => bounds.extend(p));
    }

    // Customers with an order today.
    for (const o of day.orders) {
      const c = cust.get(o.customer_id);
      if (!c) continue;
      const r = servedBy.get(o.id);
      const postponed = day.postponed.includes(o.id);
      const color = r ? driverColor(bundle, r.driver_id) : "#888";
      const sel = selectedOrder === o.id;
      const ring = highlight === "tail_lift" && c.needs_tail_lift;
      const marker = L.circleMarker(P(c), {
        radius: sel ? 10 : ring ? 8 : 6,
        color: ring ? "#111" : sel ? "#000" : "#fff",
        weight: ring || sel ? 3 : 1.5,
        fillColor: postponed ? "#fff" : color,
        fillOpacity: 1,
        dashArray: postponed ? "3 3" : undefined,
      });
      const i = r ? r.order_ids.indexOf(o.id) : -1;
      marker.bindTooltip(
        `<b>${customerName(c)}</b><br/>${TYPE_LABEL[c.type] ?? c.type} · ${o.pallets} pal.${c.needs_tail_lift ? " · hayon" : ""}<br/>` +
          `Créneau ${hhmm(c.window_start)}–${hhmm(c.window_end)}<br/>` +
          (r ? `${driverName(bundle, r.driver_id)}, arrêt ${i + 1}, livraison ${hhmm(r.service_starts[i])}` : postponed ? "<b>Reportée</b>" : ""),
      );
      marker.on("click", () => onSelectOrder?.(o.id));
      marker.addTo(g);
      bounds.extend(P(c));
    }
    L.marker(P(depot), {
      icon: L.divIcon({ className: "depot-icon", html: "<div>D</div>", iconSize: [22, 22] }),
    })
      .bindTooltip(depot.name ?? "Dépôt")
      .addTo(g);

    const key = `${bundle.run}/${day.day}`;
    if (fitted.current !== key) {
      m.fitBounds(bounds.pad(0.08));
      fitted.current = key;
    }
  }, [bundle, day, routes, highlight, selectedOrder, selectedDriver, onSelectOrder]);

  return <div className={tiles ? "map" : "map schematic"} ref={el} />;
}
