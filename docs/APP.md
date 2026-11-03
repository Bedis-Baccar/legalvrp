# APP.md — the visual application (local demo)

A local web application that shows what legalvrp does on one concrete week and on the special
cases of the law: routes on a map, each driver's day minute by minute with the legal counters,
the week's KPIs against current practice, plans executed with the true service times, and the
checker's labelled duties. Plan and decisions: [APP_PLAN.md](APP_PLAN.md).

Everything shown comes from the C++ tools and is judged by the independent checker; the
application only draws. Data are synthetic and costs illustrative (the first screen says so).

## Run it

**Windows** (MSVC + Gurobi toolchain of `scripts/build.ps1`, Python ≥ 3.11, Node.js ≥ 20 the
first time):

```powershell
./scripts/demo.ps1             # builds what is missing, then opens http://127.0.0.1:8765/
./scripts/demo.ps1 -Rebuild    # recompute every scenario
```

**Linux**:

```bash
./scripts/demo.sh                     # without Gurobi
WITH_GUROBI=ON ./scripts/demo.sh      # with Gurobi (GUROBI_HOME set)
```

Each script does four steps, each skipped when its output exists:

| step | command | output |
|---|---|---|
| C++ tools | `scripts/build.ps1 -Preset msvc-release -NoTest` (Windows), `cmake … build/release` (Linux) | `build/<preset>/apps/legalvrp-*` |
| scenarios | `python scripts/build_demo.py [--bin DIR] [--only ID…] [--force]` | `results/demo/` (bundles + `index.json`), `data/demo/` (instances) |
| web app | `cd app && npm install && npm run build` | `app/dist/` |
| server | `python scripts/demo_server.py [--port 8765] [--open]` | http://127.0.0.1:8765/ |

Building every scenario takes about 30 s without Gurobi. With Gurobi, the MILP runs (60 s/day on
`small`, 120 s/day on `medium`) add about 15 minutes. `results/`, `data/`, `app/dist` and
`app/node_modules` are git-ignored; the scenario definitions (`scenarios/`) are versioned.

Development of the front end: `cd app && npm run dev` (http://localhost:5173, hot reload; the
dev server reads `results/demo` directly).

The server is bound to 127.0.0.1 and read-only. The only outside request is the
OpenStreetMap background, made by the browser; untick **Fond de carte** to work offline.

## Screens

| screen | what it shows |
|---|---|
| **Semaine** | KPIs of each run side by side: checker verdict, cost, km, served, postponements, unserved, overtime, routes, hours Gini. Cost per day. Buttons to open any day |
| **Journées** | map of the day's routes (one colour per driver, postponed orders dotted, tail-lift customers circled when the scenario asks), list of routes with stops and breaks, details of any order, the day's checker verdict, and the drivers' timelines |
| **Timeline** | per driver: depot prep, driving, waiting, delivery, break, close. Hover a segment: times, the customer's window, *driving since the last break* (≤ 4 h 30) and *work since the last break* (≤ 6 h). Red segment = a counter over the limit; red delivery = window missed; dotted red line = end of the authorised shift |
| **Journée réelle** | per planning policy, late / illegal duties over the week; for a day, the planned and the realised timelines (the planned one in grey above) |
| **Heures des chauffeurs** | weekly hours per driver, day by day, against the contract threshold and the cap |
| **Comparaison** | small school cases: several plans of the same day, each with its timeline and the checker's verdict (rule, excess, article of law) |
| **Résultats** | frozen results computed with Gurobi (look-ahead policies) |
| **Galerie** | the 60 labelled duties of `tests/fixtures/duties.json`: timeline, expected label, checker verdict |

## Scenarios

`scenarios/*.toml` (French texts, read by `build_demo.py`; `scenarios/demo.toml` holds the title,
the disclaimer, the map anchor and the ALNS iteration count):

| id | case | how it is made |
|---|---|---|
| `semaine` | one week, 89 orders, 4 drivers: current practice vs ALNS (vs MILP with Gurobi) | `instance_small.yaml` seed 1 |
| `journee` | Monday minute by minute | the same runs |
| `reel` | plans executed with the true service times: planning rule, learned times, learned times + reserve | `legalvrp-learn`, `run-week --estimator` |
| `hayon` | the single tail-lift truck | the same runs, tail-lift customers highlighted |
| `heures` | weekly hours, part-time driver, fairness | the same runs |
| `surcharge` | heavier days: postponements with escalating penalties | `scenarios/configs/instance_overload.yaml` seed 2, not certified on purpose |
| `interim` | 6 drivers including a temp-agency driver | `instance_medium.yaml` seed 1 |
| `piege` | driving trap: 540 min of driving, every single-break position illegal | hand-built instance, hand plans + ALNS |
| `pauses` | 30-min break and 15 + 30 split vs the 45-min rule only | hand-built instances, rule switches off / on |
| `myopie` | look-ahead vs day by day vs clairvoyant | `docs/policies/lookahead.csv` (Gurobi, frozen) |
| `galerie` | the checker's labelled duties | `legalvrp-export --duties` |

**Adding a scenario**: write `scenarios/NN-name.toml` with `title`, `category`, `summary`, `story`,
`view` (`week`, `day`, `real`, `drivers`, `compare`, `report`, `gallery`), optional `compare` and
`focus`, instances (`[instances.<key>]` with `generate = {config, seed[, no_certify]}`,
`hand = "<builder>"`, or `from = "<key>"` with `rules = {…}` overrides) and runs (`[[runs]]` with
`solver = "baseline" | "alns" | "milp"` and options `iterations`, `time_limit`, `fairness`,
`estimator = "learned"` with `z` and `reserve`, `requires_gurobi`; or `plan = {sequence, breaks}`
for a hand-made plan; or `ref = "<scenario>/<run>"` to reuse a run). Then
`python scripts/build_demo.py --only <id>`.

**Real data**: an instance written with the import path of [DATA.md](DATA.md) (`week.json` with
`lat`/`lon`, `matrix.json`) is shown on its real coordinates; the names of its customers are kept.

## How it is built

```
scenarios/*.toml ─► scripts/build_demo.py ─► legalvrp-generate / -learn / -run-week --bundle / -export
                                                               │
                         results/demo/<scenario>/<run>.json  ◄─┘  (+ names and map coordinates)
                         results/demo/index.json, gallery.json
                                                               │
             app/ (Vite + React + TypeScript, Leaflet) ◄── scripts/demo_server.py (/ and /data/)
```

- **`legalvrp::viz`** (C++, [src/legalvrp/viz](../src/legalvrp/viz)): `build_timeline` walks a
  route exactly as the checker does (fixed duty start, travel from the matrix, waiting, service,
  breaks after their order, return, close) and keeps the two legal counters;
  `run_bundle` writes, per day, the orders (carried ones with their escalated penalty), the
  drivers' weekly state, the routes with their timelines, the postponed orders, the checker's
  verdict and the solver statistics (no gap for heuristic days), the week KPIs and, with
  `truth.json`, the realised days (`sim::realise_day`) and realised week KPIs; `duty_gallery`
  turns the labelled corpus into gallery items. It depends on domain, check, kpi, sim and data
  only — it never solves (`test_layering`).
- **`legalvrp-run-week --bundle FILE [--iterations N]`** writes the bundle of the run it just made
  (it knows the planning instance, also with a learned estimator and a reserve).
  `--iterations` makes ALNS deterministic.
- **`legalvrp-export`** writes the bundle of stored plans (`--instance --plans`, hand-made illegal
  plans included) or the gallery (`--duties`).
- **Tests** (`test_viz`): on every duty of the golden week (baseline and ALNS), the timeline's
  driving, work, km, duty start and end equal the checker's facts; segments tile the duty; the
  break counters follow the law (a lone 30 min resets work only, 15 + 30 resets driving); the
  bundle's KPIs equal `compute_week_kpis`; every gallery verdict equals its label.
- **Dressing** (`build_demo.py`): readable French customer names (by type), driver first names,
  and latitude/longitude by a local equirectangular projection of the synthetic plane around the
  anchor of `scenarios/demo.toml`. Times, costs and verdicts are never touched.

## Limits

- Synthetic geography: straight lines on the map, not roads; the towns sit wherever the anchor
  puts them.
- Read-only: re-running a scenario from the interface and editing a duty to see the checker's
  verdict (APP_PLAN A8) are not implemented; rebuild with `build_demo.py`.
- The look-ahead scenario shows frozen numbers; its plans are not exported (`legalvrp-policies`
  writes CSV only).
- Planners consider at most one break or 15 + 30 per duty; the law also allows e.g. two 45-min
  breaks. The `piege` scenario shows such a duty, legal for the checker and out of the planners'
  reach.
