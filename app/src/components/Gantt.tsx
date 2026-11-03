// Duty timelines, one row per driver: what the checker sees, minute by minute.
import { useState } from "react";
import type { Segment, Timeline } from "../types";
import { dur, hhmm, SEGMENT_LABEL } from "../format";
import { useWidth } from "../util";

export interface GanttRow {
  key: string;
  label: string;
  sublabel?: string;
  color: string;
  timeline: Timeline;
  windows?: Record<string, [number, number]>;   // order id -> customer window
  names?: Record<string, string>;               // order id -> customer name
  status?: "ok" | "late" | "illegal";
  shiftEnd?: number;
  ghost?: Timeline;                             // the planned timeline, drawn faintly (realised view)
}

interface Props {
  rows: GanttRow[];
  from?: number;
  to?: number;
  limits?: { drive: number; work: number };
  selectedOrder?: string;
  onSelectOrder?: (orderId: string) => void;
}

const ROW_H = 46;
const AXIS_H = 22;

const FILL: Record<string, string> = {
  idle: "var(--seg-idle)",
  prep: "var(--seg-depot)",
  close: "var(--seg-depot)",
  drive: "var(--seg-drive)",
  wait: "var(--seg-wait)",
  service: "var(--seg-service)",
  break: "var(--seg-break)",
};

export function Legend() {
  return (
    <div className="legend">
      {(["drive", "service", "wait", "break", "prep", "idle"] as const).map((k) => (
        <span key={k}>
          <i style={{ background: FILL[k] }} /> {k === "prep" ? "Dépôt (préparation, clôture)" : SEGMENT_LABEL[k]}
        </span>
      ))}
      <span>
        <i className="win" /> Créneau du client
      </span>
    </div>
  );
}

export default function Gantt({ rows, from, to, limits, selectedOrder, onSelectOrder }: Props) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const [tip, setTip] = useState<{ x: number; y: number; row: GanttRow; s: Segment } | null>(null);
  const all = rows.flatMap((r) => [r.timeline.duty_start, r.timeline.duty_end, ...(r.ghost ? [r.ghost.duty_end] : [])]);
  const t0 = from ?? Math.floor((Math.min(300, ...all) - 0) / 60) * 60;
  const t1 = to ?? Math.ceil(Math.max(1140, ...all) / 60) * 60;
  const LABEL_W = width < 640 ? 100 : 170;
  const plotW = Math.max(200, width - LABEL_W - 8);
  const x = (t: number) => LABEL_W + ((t - t0) / (t1 - t0)) * plotW;
  const height = AXIS_H + rows.length * ROW_H + 6;
  const hours: number[] = [];
  for (let h = Math.ceil(t0 / 60); h * 60 <= t1; ++h) hours.push(h * 60);

  return (
    <div className="gantt" ref={ref} onMouseLeave={() => setTip(null)}>
      <svg width={width} height={height} role="img" aria-label="Planning des chauffeurs">
        {hours.map((t) => (
          <g key={t}>
            <line x1={x(t)} x2={x(t)} y1={AXIS_H - 4} y2={height} className="grid" />
            <text x={x(t)} y={12} className="axis" textAnchor="middle">
              {hhmm(t).slice(0, 2)}h
            </text>
          </g>
        ))}
        {rows.map((r, i) => {
          const y = AXIS_H + i * ROW_H;
          const barY = y + 8;
          const barH = 20;
          return (
            <g key={r.key}>
              <rect x={0} y={y} width={width} height={ROW_H} className={i % 2 ? "row odd" : "row"} />
              <rect x={4} y={y + 10} width={5} height={ROW_H - 20} rx={2} fill={r.color} />
              <text x={14} y={y + 20} className="rowlabel">
                {LABEL_W < 170 && r.label.length > 13 ? r.label.slice(0, 12) + "…" : r.label}
              </text>
              {r.sublabel && LABEL_W >= 170 && (
                <text x={14} y={y + 35} className={`rowsub ${r.status ?? ""}`}>
                  {r.sublabel}
                </text>
              )}
              {r.shiftEnd !== undefined && r.shiftEnd <= t1 && (
                <line x1={x(r.shiftEnd)} x2={x(r.shiftEnd)} y1={y + 3} y2={y + ROW_H - 3} className="shiftend">
                  <title>Fin de service autorisée {hhmm(r.shiftEnd)}</title>
                </line>
              )}
              {r.ghost?.segments.map((s, j) => (
                <rect key={`g${j}`} x={x(s.start)} y={barY - 5} width={Math.max(0.5, x(s.end) - x(s.start))} height={3} className="ghost" />
              ))}
              {r.timeline.segments
                .filter((s) => s.kind === "service" && s.order_id && r.windows?.[s.order_id] && (s.order_id === selectedOrder || s.order_id === tip?.s.order_id))
                .map((s, j) => {
                  const [ws, we] = r.windows![s.order_id!];
                  const late = s.start > we || s.start < ws;
                  return (
                    <rect key={`w${j}`} x={x(Math.max(ws, t0))} y={barY + barH + 2} width={Math.max(1, x(Math.min(we, t1)) - x(Math.max(ws, t0)))} height={5}
                      className={late ? "window late" : "window"} />
                  );
                })}
              {r.timeline.segments.map((s, j) => {
                const over = limits && (s.drive_since_break > limits.drive || s.work_since_break > limits.work);
                const sel = selectedOrder && s.order_id === selectedOrder && (s.kind === "service" || s.kind === "wait");
                const late = s.kind === "service" && s.order_id && r.windows?.[s.order_id] && (s.start > r.windows[s.order_id][1] || s.start < r.windows[s.order_id][0]);
                return (
                  <rect key={j} x={x(s.start)} y={barY} width={Math.max(0.8, x(s.end) - x(s.start))} height={barH}
                    fill={FILL[s.kind]} className={`seg ${s.kind}${over ? " over" : ""}${sel ? " sel" : ""}${late ? " lateseg" : ""}`}
                    onMouseMove={(e) => {
                      const box = (e.currentTarget.ownerSVGElement as SVGSVGElement).getBoundingClientRect();
                      setTip({ x: e.clientX - box.left, y: e.clientY - box.top, row: r, s });
                    }}
                    onClick={() => s.order_id && onSelectOrder?.(s.order_id)} />
                );
              })}
            </g>
          );
        })}
      </svg>
      {tip && <Tip {...tip} width={width} limits={limits} />}
    </div>
  );
}

function Tip({ x, y, row, s, width, limits }: { x: number; y: number; row: GanttRow; s: Segment; width: number; limits?: { drive: number; work: number } }) {
  const name = s.order_id ? row.names?.[s.order_id] ?? s.order_id : s.kind === "drive" ? "Dépôt" : undefined;
  const w = s.order_id ? row.windows?.[s.order_id] : undefined;
  const left = Math.min(x + 14, width - 260);
  return (
    <div className="tip" style={{ left, top: y + 16 }}>
      <div className="tip-title">
        {SEGMENT_LABEL[s.kind]}
        {name ? (s.kind === "drive" ? ` → ${name}` : ` — ${name}`) : ""}
      </div>
      <div>
        {hhmm(s.start)} – {hhmm(s.end)} ({dur(s.end - s.start)})
      </div>
      {w && s.kind === "service" && (
        <div className={s.start > w[1] || s.start < w[0] ? "bad" : ""}>
          Créneau {hhmm(w[0])} – {hhmm(w[1])}
        </div>
      )}
      <div className="tip-counters">
        <span className={limits && s.drive_since_break > limits.drive ? "bad" : ""}>
          Conduite depuis la pause : {dur(s.drive_since_break)}
          {limits ? ` / ${dur(limits.drive)}` : ""}
        </span>
        <span className={limits && s.work_since_break > limits.work ? "bad" : ""}>
          Travail depuis la pause : {dur(s.work_since_break)}
          {limits ? ` / ${dur(limits.work)}` : ""}
        </span>
      </div>
    </div>
  );
}
