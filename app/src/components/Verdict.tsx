import type { Bundle, Day, Violation } from "../types";
import { dur, RULES, ruleTitle } from "../format";
import { customerName, driverName, orderCustomer } from "../util";

// Amount of a violation in words (minutes of excess for time rules, pallets for capacity).
export function amountText(v: Violation): string {
  if (!v.amount) return "";
  if (v.rule === "capacity") return `+${v.amount} palette(s)`;
  if (v.rule === "consistency") return `écart ${dur(v.amount)}`;
  return `dépassement ${dur(v.amount)}`;
}

export function ViolationList({ violations, bundle, day }: { violations: Violation[]; bundle?: Bundle; day?: Day }) {
  if (!violations.length) return <div className="badge ok big">Vérificateur : aucune infraction</div>;
  return (
    <ul className="violations">
      {violations.map((v, i) => {
        const r = RULES[v.rule];
        const c = bundle && day && v.order_id ? orderCustomer(bundle, day, v.order_id) : undefined;
        return (
          <li key={i}>
            <div className="v-title">
              <span className="badge bad">{ruleTitle(v.rule)}</span> {amountText(v)}
            </div>
            <div className="small">
              {bundle && v.driver_id ? driverName(bundle, v.driver_id) : v.driver_id}
              {v.order_id ? ` · ${c ? customerName(c) : v.order_id}` : ""}
              {r ? ` — ${r.text}` : ""}
            </div>
            {r && <div className="muted small">{r.source}</div>}
          </li>
        );
      })}
    </ul>
  );
}
