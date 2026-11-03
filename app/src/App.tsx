import { useEffect, useState } from "react";
import type { Index, ScenarioEntry, View } from "./types";
import { useIndex } from "./data";
import { Loading, RichText } from "./util";
import WeekView from "./components/WeekView";
import DayView from "./components/DayView";
import RealView from "./components/RealView";
import DriversView from "./components/DriversView";
import CompareView from "./components/CompareView";
import ReportView from "./components/ReportView";
import GalleryView from "./components/GalleryView";

// Route: #/<scenario>/<view>?run=<run>&day=<d>
interface RouteState {
  scenario?: string;
  view?: View;
  run?: string;
  day: number;
}

function parseHash(): RouteState {
  const [path, query] = window.location.hash.replace(/^#\/?/, "").split("?");
  const [scenario, view] = path.split("/").filter(Boolean);
  const q = new URLSearchParams(query ?? "");
  return { scenario, view: view as View | undefined, run: q.get("run") ?? undefined, day: Number(q.get("day") ?? 0) };
}

function toHash(s: RouteState): string {
  if (!s.scenario) return "#/";
  const q = new URLSearchParams();
  if (s.run) q.set("run", s.run);
  if (s.day) q.set("day", String(s.day));
  const qs = q.toString();
  return `#/${s.scenario}${s.view ? "/" + s.view : ""}${qs ? "?" + qs : ""}`;
}

function useRoute(): [RouteState, (s: Partial<RouteState>, replace?: boolean) => void] {
  const [state, setState] = useState<RouteState>(parseHash);
  useEffect(() => {
    const on = () => setState(parseHash());
    window.addEventListener("hashchange", on);
    return () => window.removeEventListener("hashchange", on);
  }, []);
  const go = (s: Partial<RouteState>) => {
    const next = { ...state, ...s };
    window.location.hash = toHash(next);
  };
  return [state, go];
}

const VIEW_LABEL: Record<View, string> = {
  week: "Semaine",
  day: "Journées",
  real: "Journée réelle",
  drivers: "Heures des chauffeurs",
  compare: "Comparaison",
  report: "Résultats",
  gallery: "Galerie",
};

function tabsOf(sc: ScenarioEntry): View[] {
  const hasPlans = sc.runs.some((r) => r.file);
  switch (sc.view) {
    case "week":
      return ["week", "day", "drivers"];
    case "day":
      return ["day", "week"];
    case "real":
      return ["real", "day"];
    case "drivers":
      return ["drivers", "week", "day"];
    case "compare":
      return hasPlans ? ["compare", "day"] : ["compare"];
    default:
      return [sc.view];
  }
}

function useTheme(): [string, () => void] {
  const [theme, setTheme] = useState<string>(() => {
    try {
      return localStorage.getItem("theme") ?? "";
    } catch {
      return "";
    }
  });
  useEffect(() => {
    if (theme) document.documentElement.dataset.theme = theme;
    else delete document.documentElement.dataset.theme;
  }, [theme]);
  const toggle = () => {
    const dark = theme ? theme === "dark" : window.matchMedia("(prefers-color-scheme: dark)").matches;
    const next = dark ? "light" : "dark";
    setTheme(next);
    try {
      localStorage.setItem("theme", next);
    } catch {
      /* private mode */
    }
  };
  return [theme, toggle];
}

export default function App() {
  const { data: index, error } = useIndex();
  const [route, go] = useRoute();
  const [tiles, setTiles] = useState(true);
  const [, toggleTheme] = useTheme();
  const [menu, setMenu] = useState(false);

  if (!index) {
    return (
      <div className="boot">
        <Loading error={error} />
        {error && (
          <p className="muted">
            Les scénarios n'ont pas encore été construits : lancez <code>python scripts/build_demo.py</code> à la racine du dépôt (voir docs/APP.md).
          </p>
        )}
      </div>
    );
  }
  const sc = index.scenarios.find((s) => s.id === route.scenario);
  return (
    <div className="shell">
      <header className="top">
        <button className="menu-btn" onClick={() => setMenu((m) => !m)} aria-label="Scénarios">
          ☰
        </button>
        <a className="brand" href="#/">
          <span className="logo">⛟</span> {index.title}
        </a>
        <span className="sub">{index.subtitle}</span>
        <div className="top-actions">
          <label className="switch" title="Fond de carte OpenStreetMap (nécessite internet)">
            <input type="checkbox" checked={tiles} onChange={(e) => setTiles(e.target.checked)} /> <span className="hide-sm">Fond de carte</span><span className="show-sm">Carte</span>
          </label>
          <button className="icon" onClick={toggleTheme} title="Thème clair / sombre">
            ◐
          </button>
        </div>
      </header>
      <div className="body">
        <nav className={menu ? "side-nav open" : "side-nav"} onClick={() => setMenu(false)}>
          <Nav index={index} current={sc?.id} />
        </nav>
        <main>
          {sc ? (
            <Scenario key={sc.id} sc={sc} route={route} go={go} tiles={tiles} />
          ) : (
            <Home index={index} />
          )}
        </main>
      </div>
    </div>
  );
}

function Nav({ index, current }: { index: Index; current?: string }) {
  const cats: string[] = [];
  const visible = index.scenarios.filter((s) => !s.hidden);
  visible.forEach((s) => cats.includes(s.category) || cats.push(s.category));
  return (
    <>
      <a href="#/" className={!current ? "nav-item on" : "nav-item"}>
        Accueil
      </a>
      {cats.map((c) => (
        <div key={c}>
          <div className="nav-cat">{c}</div>
          {visible
            .filter((s) => s.category === c)
            .map((s) => (
              <a key={s.id} href={`#/${s.id}`} className={s.id === current ? "nav-item on" : "nav-item"}>
                {s.title}
              </a>
            ))}
        </div>
      ))}
      <div className="nav-foot muted small">
        Construit le {new Date(index.built).toLocaleString("fr-FR")}
        <br />
        Gurobi : {index.gurobi ? "oui" : "non"}
      </div>
    </>
  );
}

function Home({ index }: { index: Index }) {
  return (
    <div className="stack">
      <section className="hero">
        <h1>{index.title}</h1>
        <p className="lead">{index.subtitle}.</p>
        <div className="notice warn">{index.disclaimer}</div>
        {index.scenarios.some((s) => s.id === index.client?.scenario) && (
          <p>
            <a className="btn" href="client.html">
              Ouvrir la maquette client ({index.client?.product}) →
            </a>
          </p>
        )}
      </section>
      <div className="cards">
        {index.scenarios.filter((s) => !s.hidden).map((s) => (
          <a key={s.id} className="card scenario-card" href={`#/${s.id}`}>
            <div className="nav-cat">{s.category}</div>
            <h3>{s.title}</h3>
            <p>{s.summary}</p>
            <span className="link">Ouvrir →</span>
          </a>
        ))}
      </div>
    </div>
  );
}

function Scenario({ sc, route, go, tiles }: { sc: ScenarioEntry; route: RouteState; go: (s: Partial<RouteState>) => void; tiles: boolean }) {
  const tabs = tabsOf(sc);
  const view = route.view && tabs.includes(route.view) ? route.view : tabs[0];
  const [story, setStory] = useState(true);
  const focus = sc.focus[0];
  const runs = sc.runs.filter((r) => r.file);
  const defaultRun = route.run ?? focus?.run ?? (view === "real" ? sc.compare[0] : runs[runs.length - 1]?.id) ?? runs[0]?.id ?? "";
  const day = route.run || route.day ? route.day : focus?.day ?? 0;
  const unavailable = sc.runs.filter((r) => r.unavailable);
  return (
    <div className="stack">
      <section className="scenario-head">
        <div className="nav-cat">{sc.category}</div>
        <h1>{sc.title}</h1>
        <p className="lead">{sc.summary}</p>
        <button className="link" onClick={() => setStory((x) => !x)}>
          {story ? "Masquer l'explication" : "Afficher l'explication"}
        </button>
        {story && (
          <div className="story">
            <RichText text={sc.story} />
          </div>
        )}
        {unavailable.length > 0 && (
          <div className="notice small">
            Non construit sur cette machine : {unavailable.map((r) => `${r.label} (${r.unavailable})`).join(" ; ")}.
          </div>
        )}
      </section>
      {tabs.length > 1 && (
        <div className="tabs">
          {tabs.map((t) => (
            <button key={t} className={t === view ? "on" : ""} onClick={() => go({ view: t })}>
              {VIEW_LABEL[t]}
            </button>
          ))}
        </div>
      )}
      {view === "week" && <WeekView sc={sc} onOpenDay={(run, d) => go({ view: "day", run, day: d })} />}
      {view === "day" && <DayView sc={sc} run={defaultRun} day={day} tiles={tiles} onChange={(run, d) => go({ view: "day", run, day: d })} />}
      {view === "real" && <RealView sc={sc} run={route.run ?? sc.compare[0]} day={day} onChange={(run, d) => go({ view: "real", run, day: d })} />}
      {view === "drivers" && <DriversView sc={sc} />}
      {view === "compare" && <CompareView sc={sc} />}
      {view === "report" && <ReportView sc={sc} />}
      {view === "gallery" && <GalleryView sc={sc} />}
    </div>
  );
}
