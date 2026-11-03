// Client mock-up: a dispatch and compliance tool built on the run bundles of one scenario.
import { useEffect, useState } from "react";
import type { Bundle, Index, RunEntry, ScenarioEntry } from "../types";
import { useBundle, useIndex } from "../data";
import { Loading } from "../util";
import Dashboard from "./pages/Dashboard";
import Planning from "./pages/Planning";
import Drivers from "./pages/Drivers";
import Orders from "./pages/Orders";
import Audit from "./pages/Audit";

export type Page = "dashboard" | "planning" | "drivers" | "orders" | "audit";

export interface Nav {
  page: Page;
  plan: string;
  day: number;
  driver?: string;
  order?: string;
  real?: boolean;
}

export interface Ctx {
  index: Index;
  sc: ScenarioEntry;
  run: RunEntry;
  bundle: Bundle;
  reference?: Bundle;   // the current practice, for comparisons
  nav: Nav;
  go: (n: Partial<Nav>) => void;
}

const PAGES: { id: Page; label: string; icon: string }[] = [
  { id: "dashboard", label: "Tableau de bord", icon: "▦" },
  { id: "planning", label: "Planning du jour", icon: "▤" },
  { id: "drivers", label: "Chauffeurs", icon: "◉" },
  { id: "orders", label: "Commandes", icon: "☰" },
  { id: "audit", label: "Contrôle légal", icon: "✓" },
];

function parse(defaultPlan: string): Nav {
  const [path, query] = window.location.hash.replace(/^#\/?/, "").split("?");
  const q = new URLSearchParams(query ?? "");
  const page = (PAGES.find((p) => p.id === path)?.id ?? "dashboard") as Page;
  return {
    page,
    plan: q.get("plan") ?? defaultPlan,
    day: Number(q.get("day") ?? 0),
    driver: q.get("driver") ?? undefined,
    order: q.get("order") ?? undefined,
    real: q.get("real") === "1",
  };
}

function hash(n: Nav): string {
  const q = new URLSearchParams();
  q.set("plan", n.plan);
  if (n.day) q.set("day", String(n.day));
  if (n.driver) q.set("driver", n.driver);
  if (n.order) q.set("order", n.order);
  if (n.real) q.set("real", "1");
  return `#/${n.page}?${q.toString()}`;
}

export default function ClientApp() {
  const { data: index, error } = useIndex();
  if (!index) return <div className="boot"><Loading error={error} /></div>;
  const conf = index.client;
  const sc = index.scenarios.find((s) => s.id === (conf?.scenario ?? "client"));
  if (!conf || !sc) {
    return (
      <div className="boot">
        <div className="notice error">Le scénario de la maquette n'est pas construit : lancez <code>python scripts/build_demo.py --only client</code>.</div>
      </div>
    );
  }
  return <Shell index={index} sc={sc} defaultPlan={conf.default_run} />;
}

function Shell({ index, sc, defaultPlan }: { index: Index; sc: ScenarioEntry; defaultPlan: string }) {
  const [nav, setNav] = useState<Nav>(() => parse(defaultPlan));
  useEffect(() => {
    const on = () => setNav(parse(defaultPlan));
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, [defaultPlan]);
  const go = (n: Partial<Nav>) => {
    window.location.hash = hash({ ...nav, ...n });
  };
  const runs = sc.runs.filter((r) => r.file);
  const run = runs.find((r) => r.id === nav.plan) ?? runs.find((r) => r.id === defaultPlan) ?? runs[0];
  const refRun = sc.runs.find((r) => r.solver === "baseline" && r.file);
  const { data: bundle, error } = useBundle(run?.file);
  const { data: reference } = useBundle(refRun?.file);
  const [dark, setDark] = useState(() => document.documentElement.dataset.theme === "dark");
  useEffect(() => {
    document.documentElement.dataset.theme = dark ? "dark" : "light";
  }, [dark]);
  const conf = index.client!;

  return (
    <div className="c-shell">
      <aside className="c-side">
        <div className="c-brand">
          <span className="c-logo">⛟</span>
          <span>{conf.product}</span>
        </div>
        <nav>
          {PAGES.map((p) => (
            <a key={p.id} href={hash({ ...nav, page: p.id, order: undefined })} className={p.id === nav.page ? "on" : ""}>
              <span className="c-ico">{p.icon}</span>
              {p.label}
            </a>
          ))}
        </nav>
        <div className="c-side-foot">
          <div>{conf.company}</div>
          <div className="muted">Maquette — données synthétiques, coûts illustratifs</div>
          <a href="index.html">← démonstration</a>
        </div>
      </aside>
      <div className="c-main">
        <header className="c-top">
          <div className="c-title">
            <div className="c-kicker">Semaine type · 5 jours · {bundle ? `${bundle.instance.drivers.length} chauffeurs · ${bundle.instance.trucks.length} porteurs` : ""}</div>
            <h1>{PAGES.find((p) => p.id === nav.page)?.label}</h1>
          </div>
          <div className="c-top-actions">
            <label className="c-select">
              <span>Plan</span>
              <select value={run?.id} onChange={(e) => go({ plan: e.target.value })}>
                {sc.runs.map((r) => (
                  <option key={r.id} value={r.id} disabled={!r.file}>
                    {r.label}
                    {r.unavailable ? " (non construit)" : ""}
                  </option>
                ))}
              </select>
            </label>
            <button className="icon" onClick={() => setDark((d) => !d)} title="Thème clair / sombre">
              ◐
            </button>
          </div>
        </header>
        <div className="c-content">
          {!bundle || !run ? (
            <Loading error={error} />
          ) : (
            <Page ctx={{ index, sc, run, bundle, reference: refRun?.id === run.id ? undefined : reference, nav, go }} />
          )}
        </div>
      </div>
    </div>
  );
}

function Page({ ctx }: { ctx: Ctx }) {
  switch (ctx.nav.page) {
    case "planning":
      return <Planning ctx={ctx} />;
    case "drivers":
      return <Drivers ctx={ctx} />;
    case "orders":
      return <Orders ctx={ctx} />;
    case "audit":
      return <Audit ctx={ctx} />;
    default:
      return <Dashboard ctx={ctx} />;
  }
}
