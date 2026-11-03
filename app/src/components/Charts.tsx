// Small hand-written SVG charts (grouped bars). One colour per run, the first run in grey.
import { useWidth } from "../util";

export const RUN_COLORS = ["#8a94a6", "#2563eb", "#0f9d58", "#e8590c", "#7048e8", "#c2255c"];

export interface Series {
  label: string;
  values: (number | null)[];
}

export function GroupedBars({ groups, series, format, height = 220, colors = RUN_COLORS }: {
  groups: string[];
  series: Series[];
  format: (v: number) => string;
  height?: number;
  colors?: string[];
}) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const max = Math.max(1, ...series.flatMap((s) => s.values.map((v) => v ?? 0)));
  const left = 8;
  const bottom = 26;
  const top = 18;
  const gw = (width - left) / Math.max(1, groups.length);
  const bw = Math.min(46, (gw - 16) / Math.max(1, series.length));
  const y = (v: number) => top + (1 - v / max) * (height - top - bottom);
  return (
    <div ref={ref} className="chart">
      <svg width={width} height={height}>
        <line x1={left} x2={width} y1={height - bottom} y2={height - bottom} className="grid" />
        {groups.map((g, gi) => {
          const x0 = left + gi * gw + (gw - bw * series.length) / 2;
          return (
            <g key={g}>
              {series.map((s, si) => {
                const v = s.values[gi];
                if (v === null || v === undefined) return null;
                return (
                  <g key={si}>
                    <rect x={x0 + si * bw + 1} y={y(v)} width={bw - 2} height={height - bottom - y(v)} rx={2} fill={colors[si % colors.length]}>
                      <title>{`${s.label} — ${g} : ${format(v)}`}</title>
                    </rect>
                    {bw > 30 && (
                      <text x={x0 + si * bw + bw / 2} y={y(v) - 4} textAnchor="middle" className="barlabel">
                        {format(v)}
                      </text>
                    )}
                  </g>
                );
              })}
              <text x={left + gi * gw + gw / 2} y={height - 8} textAnchor="middle" className="axis">
                {g}
              </text>
            </g>
          );
        })}
      </svg>
    </div>
  );
}

export function SeriesLegend({ labels, colors = RUN_COLORS }: { labels: string[]; colors?: string[] }) {
  return (
    <div className="legend">
      {labels.map((l, i) => (
        <span key={l}>
          <i style={{ background: colors[i % colors.length] }} /> {l}
        </span>
      ))}
    </div>
  );
}
