# APP_PLAN.md — local visual application (V2-A)

Status: **proposal** (2026-11-03), nothing implemented yet. Decisions to take: §8.

Goal: a local application that shows, on one concrete week, what legalvrp does — routes on a
map, each driver's day on a timeline with the legal breaks, the cost against current practice —
and a catalogue of special cases, each one explaining a rule or a result of V0/V1. It runs on
the developer's machine (Windows or Linux), **without Gurobi**, from one command.

The engine is not changed. The application reads what the C++ tools write, plus two small new
outputs (§5): a timeline export and the realised day.

---

## 1. Audit of the data: can the application rely on it?

### 1.1 Correct — yes

The data is internally consistent and checked at every stage. It is a sound base for the
application.

| What | Guarantee | Where |
|---|---|---|
| Instances | seeded, byte-identical on every platform, validated (`data::validate_week`: ids, windows, matrix shape, integers) | `src/legalvrp/data`, golden fixtures `tests/fixtures/instances/{tiny,small}/1` |
| Feasibility | every generated week is *certified*: the baseline plays the whole week before the instance is accepted | `week/certify`, D-027 |
| Plans | every reported plan is re-checked by an independent checker that links only the data model; KPIs are recomputed by the checker | `check/`, `kpi/` |
| The law | 60 hand-labelled duties (21 legal, 39 illegal with the exact rule broken), boundary cases at 270/271 and 360/361 min, both traps, the V1 break options | `tests/fixtures/duties.json` |
| Uncertainty | true service times on a separate RNG stream, never read by planners | `truth.json`, V1-T6 |

Verified on 2026-11-03 in this repository (Linux, GCC, no Gurobi, release build), `small` seed 1:

| | baseline (territories) | ALNS (5 s/day) |
|---|---|---|
| week cost | 6 869 € | **4 128 € (−40 %)** |
| km | 2 858 | 2 542 |
| unserved at the end of the week | 1 | 0 |
| checker | OK | OK, 0 violations |

### 1.2 Realistic — partly; this must be said in the application

| Element | Status | Consequence for the application |
|---|---|---|
| Geography | synthetic: 3–5 towns 15–60 km from the depot, customers spread 3–6 km, **planar km coordinates, no lat/lon** | no real map as-is; see A0 (geo-dressing) |
| Travel times | euclidean km × 1.3, 55 km/h, + 4 min per leg, ±5 % asymmetric noise | plausible for regional distribution, not a road network |
| Customers | three types (grocery 06:00–10:00, restaurant 08:30–11:30, site 07:00–15:00), names `site 1`, `grocery 3` | readable names needed (A0) |
| Rules | real texts (Reg. 561/2006, Dir. 2002/15, Code des transports), values in `config/rules.yaml` | can be shown as such |
| Costs | **illustrative** (1.60 €/km, overtime 0.45 €/min, agency 0.55 €/min + 60 €/day, postponement 200 € doubling) | label every euro as "illustrative costs"; relative gains are the message |
| Service times | lognormal by customer type, invented parameters | fine for the demo, to be learned on real history later |

**Verdict.** The base is *correct* (consistent, validated, legally checked) but *synthetic*. That
is right for a demo, provided the application says it on its first screen ("données
synthétiques, coûts illustratifs"). For a pitch to a carrier, the next step is real data
through the existing import path (`docs/DATA.md`: `week.json` + `matrix.json`, no code change);
it is out of this plan's scope.

### 1.3 What the application needs and does not exist yet

1. **Coordinates for a map**: `lat`/`lon` are optional in `week.json` and never filled.
2. **The detailed timeline of a duty** (prep, drive, wait, service, break, close, with the
   counters "driving since break" and "work since break"). `plan.json` gives arrivals, service
   starts, break positions and lengths, departure and return. The rest is computable, but only
   the route evaluator / checker should compute it: the law must not be re-implemented in
   JavaScript.
3. **The realised day as files**: `sim::realise_day` exists (V1-T6) but `legalvrp-robustness`
   only writes aggregated CSV/Markdown, not realised plans per duty.
4. **The special cases as instances**: the traps and the break cases exist as hand-built test
   data inside `tests/` (C++ and `duties.json`), not as instance directories a tool can run.
5. **A wrong figure**: the week report prints `gap 0.0%` for ALNS days, which have no bound.
   It must read `—` (or the CG bound when one is available).

---

## 2. The concrete weekly example

**Main example: `small`, seed 1** (already generated and verified above).

- 4 drivers: 3 full-time (shifts 05:30, 06:00, 07:00) and 1 **part-time** (24 h/week, 06:00).
- 4 trucks of 12–18 pallets; **only one has a tail-lift** (12 pallets).
- 40 customers in 3–4 towns, 89 orders over Monday–Friday (20, 19, 15, 20, 15); Monday and
  Thursday heavier.
- Story told by the screens: Monday plan → the four duties on the timeline with their breaks →
  the week's hours per driver against the 39 h / 24 h thresholds → baseline vs ALNS.

**Second example: `medium`, seed 1** (25–30 orders/day, 6 drivers including one
**temp-agency** driver paid per minute + day fee): shows when the planner calls the agency driver
and when it avoids him.

Both are produced by the existing tools; the application ships the scenario *definitions* (§4),
not the generated files (`data/` and `results/` stay git-ignored).

---

## 3. Catalogue of special cases

Each case is a scenario card: one sentence of context, the rule involved, what to look at, and
the result. "Source" says where the case already exists.

| # | Case | What the user sees | Source | Gurobi |
|---|---|---|---|---|
| C1 | **Normal week**, current practice vs optimisation | KPI cards, cost per day, maps side by side | `small/1`, run-week baseline / alns | no |
| C2 | **Driving trap**: 3 customers whose legs (60/140/200/140 min) cannot be split into two blocks ≤ 4 h 30 by any break position | the four break positions tried, each red with the rule broken; the optimum postpones one customer | trap (a), T6 / `duties.json` | no (ALNS + checker) |
| C3 | **Break that makes a customer late**: a route legal only if the 45 min are taken *before* the next arrival / after waiting | same route, break early (red: 385 min of work after) vs break late (green) | `duties.json` "legal only late" | no |
| C4 | **Short breaks of the law**: lone 30 min (6–9 h of work) and 15 + 30 split serving an order the 45-min rule would postpone | same day with V0 breaks vs V1 breaks | V1-T8 hand cases, `legalvrp-breaks` | no |
| C5 | **Tail-lift**: tail-lift customers can only be served by the one 12-pallet truck | highlighted stops, the tail-lift truck's route, what happens when it is full | `small/1` data | no |
| C6 | **Weekly caps and the part-time driver**: hours accumulate, Thursday/Friday plans change because of the 24 h threshold | weekly hours bar per driver, day by day | `small/1` week state | no |
| C7 | **Overloaded day → postponement**: more pallets than the fleet; postponement costs 200, 400, 800 € | which orders move to which day, and why | generated with a raised `orders_per_day` | no |
| C8 | **Agency driver** | when the 60 € fee + 0.55 €/min is worth paying | `medium/1` | no |
| C9 | **The real day**: Monday's optimised plan executed with the true service times | planned vs realised timeline; late stops in orange, illegal duties in red (40–49 % of duties in V1-T6) | `truth.json` + `sim::realise_day` (new export, A2) | no |
| C10 | **Planning with learned times and a reserve** | the same day planned with the learned estimator: most duties back to green, +15–28 % cost | `legalvrp-learn` + run-week `--estimator` | no |
| C11 | **Myopia**: a scarce skill held by a driver near his weekly cap; day-by-day planning pays up to 45 % | myopic week vs look-ahead week | T11 trap (869.40 vs 595.20), `legalvrp-policies` | **yes**: precomputed on the Gurobi machine and shipped as a frozen result, or left out |
| C12 | **The checker as a referee**: a plan edited by hand (stop moved, break removed) | the violation list with the article of law | `legalvrp-check --plan` | no (phase 2, needs the server) |

C2, C3 and C4 need small hand-built instances; the builder (A3) writes them as ordinary instance
directories (`week.json` + full `matrix.json`), so every tool runs on them unchanged.

---

## 4. Architecture

```
scenarios/*.yaml ──► scripts/build_demo.py ──► C++ tools (generate, run-week, check,
 (checked in)          (orchestrator)            export, realise)              │
                                                                               ▼
                                         results/demo/<scenario>/bundle.json (git-ignored)
                                                                               │
                      app/ (static web UI, built once) ◄── local server ◄──────┘
                                                        (phase 1: serves files;
                                                         phase 2: also re-runs scenarios)
```

- **C++ stays the only place where the law is computed.** Two new tools (A1, A2) export
  timelines and realised days as JSON; the UI only draws.
- **Bundle** (`bundle.json`, one per scenario): scenario text, instance (customers with lat/lon,
  drivers, trucks), per day and per solver: routes, timeline segments, checker verdict and
  violations, KPIs; week KPIs; optional realised day.
- **Orchestrator**: one Python script (standard library only), reads `scenarios/*.yaml`, calls
  the binaries, assembles the bundles. Same script on Windows and Linux.
- **Local server**:
  - phase 1: `python -m http.server` on `app/dist` + `results/demo` — read-only, nothing can
    fail during a demo;
  - phase 2: a small Python server (stdlib `http.server`, or FastAPI if we accept a pip
    dependency) with two endpoints: `POST /run` (re-run a scenario with other parameters: seed,
    solver, time limit, z/reserve) and `POST /check` (C12).
- **Front end**: Vite + React + TypeScript, built once to static files.
  - map: Leaflet with OpenStreetMap tiles when online, plain SVG schematic plane offline (the
    coordinates are synthetic anyway);
  - timeline (Gantt): hand-written SVG, one row per driver, minute resolution, windows drawn
    behind the stops;
  - charts: hand-written SVG (a few bars and lines do not justify a chart library).
  - UI language: French.

Why not embed an HTTP server in C++ (cpp-httplib)? It is possible and gives one binary, but it
adds C++ work for no gain in a local demo; the Python orchestrator is also what the batch
precomputation needs.

### Screens

1. **Accueil** — the example week in one sentence, the warning "données synthétiques, coûts
   illustratifs", the scenario list.
2. **Semaine** — KPI cards baseline vs optimisation (cost, km, postponements, unserved, overtime,
   hours fairness), cost per day, weekly hours per driver against the thresholds.
3. **Jour** — day selector; map with one colour per driver; Gantt below; click a stop → order,
   pallets, window, tail-lift, planned and true service time; per duty, the legal counters and
   the checker verdict.
4. **Cas particuliers** — the cards of §3, each opening the Jour screen on the right day and
   driver with an explanatory text.
5. **Journée réelle** — planned vs realised Gantt for the same day, three policies (V0 rule,
   learned, learned + reserve).

---

## 5. Tasks

Same rules as V0/V1: each task ends with tests where applicable, its acceptance criteria met, and
the checker passing on every plan shown.

| Task | Content | Acceptance | Estimate |
|---|---|---|---|
| **A0** | Data dressing: (i) optional anchor `geo: {lat, lon}` in instance configs, converting km to WGS84 (local equirectangular projection) into `lat`/`lon`; (ii) readable customer names from a fixed list; (iii) week report prints `—` for the gap of ALNS days | golden hashes of `week.json` change only by the new fields (or not at all when `geo` is absent); unit tests unchanged otherwise | 0.5 d |
| **A1** | `legalvrp-export --instance … --plans … --out bundle_part.json`: for each duty, timeline segments `{kind: prep|drive|wait|service|break|close, start, end, order_id?}` and the counters, produced by the exact route evaluator; checker verdict and violations per day | segments tile `[shift_start, return + close]` with no gap; their sums equal the checker's driving and work times on every duty of `small/1` and of the 60-duty corpus | 1 d |
| **A2** | `legalvrp-realise`: executes a plan directory with `truth.json`, writes realised `plan.json` and per-duty outcomes (late stops, broken rules, delay, overrun) | figures equal those of `legalvrp-robustness` on the same seed and policy | 0.5 d |
| **A3** | `scenarios/*.yaml` (C1–C10) and `scripts/build_demo.py`: hand-built instances for C2–C4, runs, bundles | one command builds all bundles from a clean clone in < 5 min with the ALNS at 5 s/day; every plan passes the checker | 1.5 d |
| **A4** | Front end: Accueil, Semaine, Jour (map + Gantt) | renders `small/1` and `medium/1`; works at 1280 px and on a laptop screen; no network needed except map tiles | 2 d |
| **A5** | Cas particuliers screen and texts (French) | each of C1–C10 opens on the right day/driver and its text matches the bundle's figures | 1 d |
| **A6** | Journée réelle screen | planned vs realised on Monday of `small/1`, three policies | 0.5 d |
| **A7** | One-command launch: `scripts/demo.ps1` and `scripts/demo.sh` (build if needed, build bundles if missing, start the server, open the browser) + `docs/APP.md` | works from a clean clone on Windows (MSVC) and Linux (GCC), without Gurobi | 0.5 d |
| *A8 (phase 2)* | Server endpoints `/run` and `/check`; C12 (edit a duty, see the violations) | a re-run with another seed shows a new week in < 1 min; an edited duty shows the checker's exact rule keys | 1.5 d |
| *A9 (optional)* | C11 frozen from a Gurobi run (`legalvrp-policies`) | bundle produced on the Windows/Gurobi machine, committed under `docs/` like other V1 results | 0.5 d |

**Phase 1 (A0–A7): about 7.5 days** — a complete, safe demo. Phase 2 (A8–A9): about 2 days.

Order: A0 → A1 → A3 (with C1 only) → A4 → first demo possible; then A2, the other scenarios, A5,
A6, A7.

### Repository layout added

```
app/                 front end (Vite + React + TS); app/dist is git-ignored
scenarios/           scenario definitions (YAML, French texts)
scripts/build_demo.py, scripts/demo.{ps1,sh}
apps/export.cpp, apps/realise.cpp
docs/APP.md          how to run the demo
results/demo/        generated bundles (git-ignored, already covered by /results/*)
```

---

## 6. Requirements on the demo machine

- The existing C++ toolchain (CMake, Ninja, MSVC or GCC); **no Gurobi needed** for C1–C10.
- Python ≥ 3.10 (standard library only in phase 1).
- Node.js ≥ 20 only to build the front end; the built `app/dist` can be copied to a machine
  without Node.
- A browser. Internet only for map tiles; offline, the map falls back to the schematic plane.

---

## 7. Risks

| Risk | Mitigation |
|---|---|
| The audience takes synthetic figures for real ones | warning on the first screen; "coûts illustratifs" next to every euro; the message is the *relative* gain and the 0 violations |
| A live re-run is slow or fails | phase 1 is precomputed; the ALNS is deterministic with a fixed iteration count (V1-T10), so bundles are reproducible |
| Timeline drawn by the UI disagrees with the checker | segments come from the C++ evaluator (A1) and are tested against the checker's totals |
| A fake map suggests real roads | lines are straight segments, labelled "trajets schématiques" |
| Scope creep (editing, multi-depot, re-planning during the day) | phase 2 limited to `/run` and `/check`; the rest stays in the V2 list of `V1_PLAN.md` |

---

## 8. Decisions to take

- **Q1 — Stack**: Vite + React + TS front end with a Python stdlib orchestrator/server
  (proposed), or a single static HTML page without a build step (faster, less maintainable).
- **Q2 — Interactivity in the first version**: read-only precomputed scenarios (proposed), or
  re-running from the UI (A8) from the start.
- **Q3 — Geography**: anchor the synthetic towns around a real city (which one?) for an
  OpenStreetMap background, or keep the schematic plane only.
- **Q4 — C11 (myopia)**: freeze it from a Gurobi run (A9) or leave it out of the demo.
- **Q5 — Audience**: technical (emphasis on the timeline, the law, the certified bounds) or
  business (map, euros, the real day). It changes the order of the screens and the texts, not
  the tasks.
