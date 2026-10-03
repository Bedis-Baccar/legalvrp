# Project brief — `legalvrp` V0

**Original V0 specification**, written before implementation (it assumed Python + gurobipy). The repository implements it in C++20; every deviation and every decision taken since is logged in [`DECISIONS.md`](DECISIONS.md), and [`MODEL.md`](MODEL.md) is the single source of truth for the mathematics.

---

## 0. How to work in this repository

1. **Follow the task order in §12.** Do not start a task before the previous one meets its acceptance criteria.
2. **Small, reviewable steps.** One task per branch or commit series. Show the plan for a task before implementing it if it touches more than three files.
3. **The model is specified, not invented.** Implement §6 exactly. If you believe the formulation is wrong or incomplete, stop and explain why with a concrete counter-example; do not silently change it. Any accepted change is made in `docs/MODEL.md` first, then in code, and logged in `docs/DECISIONS.md`.
4. **The checker is the judge.** No plan is reported, plotted or saved as a result unless `check/checker.py` returns zero violations for it. The checker must never import from `model/`, `heuristics/` or `week/`.
5. **Ask before adding a dependency** not listed in §10.
6. **No ML in V0.** Keep the hooks described in §9; do not implement estimators beyond the deterministic one.
7. **Never call the network during solving or tests.** External data is fetched once by an explicit command and cached.
8. **Units are integer minutes and integer pallets** everywhere in instance data. Costs are floats in euros.

---

## 1. The problem in one paragraph

A regional distributor delivers pallets from one depot to shops, restaurants and building sites with rigid trucks over 3.5 t. Every afternoon it plans the next day: which driver serves which order, in which sequence, where the driver takes the legally required break, and which orders are postponed to the following day. Plans must respect truck capacity, customer receiving windows, equipment (tail-lift), EU driving-time rules, EU and French working-time rules, each driver's shift, and the hours each driver has already worked this week. The objective is the cost of the day: distance, variable hours, overtime and postponements. V0 plans one week, Monday to Friday, as five consecutive daily optimisations that pass the drivers' weekly state from one day to the next.

---

## 2. V0 scope

**In scope:** synthetic instance generation; an exact daily MILP solved with Gurobi; a territory-based baseline heuristic that also provides the MIP start; a rolling Monday–Friday loop; an independent legality checker; KPI computation; a scaling experiment measuring where the MILP stops being solvable.

**Out of scope (later versions, do not build):** machine learning; stochastic durations; ALNS or any metaheuristic beyond the baseline; column generation; C++; multi-depot; intraday re-planning; driver–truck swaps; a user interface; any LLM component.

**Instance sizes**

| Name | Days | Drivers | Orders per day | Used for |
|---|---|---|---|---|
| `tiny` | 1 | 2 | 6–8 | unit tests, CI (must fit the pip restricted licence) |
| `small` | 5 | 4 | 15–20 | the main V0 result |
| `scale` | 1 | ⌈n/5⌉ | n ∈ {8, 10, 12, 15, 20, 25, 30, 40} | scaling experiment |

---

## 3. Domain rules (authoritative parameter table)

All values live in `config/rules.yaml`. Code reads them from there; no literal appears in the model code.

| Key | Value | Meaning | Source |
|---|---|---|---|
| `drive_before_break` | 270 | max driving minutes before a break | Reg. (EC) 561/2006 art. 7 |
| `work_before_break` | 360 | max work minutes before a break | Dir. 2002/15/EC art. 5; C. transports L3312-2 |
| `break_length` | 45 | break duration (one break, unsplit, in V0) | both of the above |
| `daily_drive_max` | 540 | max driving per day (10 h extension disabled in V0) | 561/2006 art. 6 |
| `daily_service_max` | 720 | max *temps de service* per day | C. transports D3312-51 |
| `weekly_drive_max` | 3360 | max driving per week | 561/2006 art. 6 |
| `daily_rest_min` | 660 | min rest between consecutive duties | 561/2006 art. 8 |
| `depot_prep` | 20 | pre-trip work at the depot before departure | company parameter |
| `depot_close` | 10 | post-trip work at the depot after return | company parameter |

**Contracts** (`config/contracts.yaml`), per contract class:

| Key | Full-time short-distance | Part-time | Temp-agency |
|---|---|---|---|
| `weekly_threshold` (overtime starts above) | 2340 (39 h) | contract hours, e.g. 1440 (24 h) | 2100 (35 h) |
| `weekly_service_max` | 3120 (52 h) | 1584 (contract + 10 %) | 3120 |
| `cost_per_min_regular` | 0.0 (already paid) + tie-breaker 0.01 | 0.0 + 0.01 | agency rate |
| `cost_per_min_extra` | overtime rate | complementary-hours rate | agency rate |
| `fixed_cost_if_used` | 0 | 0 | agency day fee |

Rationale to respect: **a salaried driver's hours below the contract threshold are already paid**, so they carry only a tie-breaker cost. The economic levers are kilometres, hours above the threshold, temp usage and postponements. All monetary defaults are illustrative and labelled as such in the config.

**Interpretation rules (V0 modelling choices — conservative, documented in `docs/MODEL.md`):**

- Exactly one break of 45 minutes may be taken per duty, at a customer, immediately after service. A duty without a break must have total work ≤ 360 and total driving ≤ 270.
- *Work* = all elapsed time on duty except the break, including waiting. *Temps de service* = the same quantity, plus `depot_prep` and `depot_close`.
- Daily rest holds automatically when a driver's shift start is the same every day (on-duty span ≤ 765 minutes). The week loop still verifies it.

---

## 4. Data model

Implement as typed, immutable records (pydantic v2 models or frozen dataclasses) in `domain/models.py`. Field names below are the names to use.

**Customer** — `id`, `name`, `type` ∈ {grocery, restaurant, site}, `x_km`, `y_km` (synthetic) or `lat`, `lon` (real), `window_start`, `window_end` (minutes from midnight), `needs_tail_lift` (bool). No access restriction: any truck can deliver any customer (D-008).

**Order** — `id`, `customer_id`, `day` (0 = Monday), `pallets`, `service_mu` (minutes), `service_sigma` (minutes, **always 0 in V0**), `postponed_from` (day or null), `postpone_penalty` (€).

**Truck** — `id`, `capacity_pallets`, `has_tail_lift`.

**Driver** — `id`, `contract_class`, `truck_id`, `shift_start` (minutes), `shift_end_max` (minutes), `available_days` (list).

**DriverWeekState** — `driver_id`, `service_minutes_week`, `driving_minutes_week`, `last_duty_end` (absolute minutes since Monday 00:00 or null).

**Matrix** — `node_ids`, `time_min` (integer minutes, asymmetric allowed), `dist_km` (float).

**DayInstance** — `day`, `depot`, `orders`, `drivers`, `trucks`, `states`, `matrix`, `rules`, `contracts`, `costs`.

**Route** — `driver_id`, `order_ids` (sequence), `break_after_order_id` (or null), `departure`, `arrivals`, `service_starts`, `return_time`.

**DayPlan** — `day`, `routes`, `postponed_order_ids`, `objective`, `solver_stats`.

**Compatibility of driver $k$ and order $i$** (precomputed once per day): tail-lift if the customer needs one, and `pallets` ≤ capacity. Truck size never restricts a customer (D-008). Incompatible pairs produce no variables.

---

## 5. Instance generation (`data/`)

### 5.1 Mode A — synthetic (default, offline)

Parameters in `config/instance_<name>.yaml`; every random draw uses a seeded `numpy.random.Generator`.

| Element | Rule |
|---|---|
| Geography | Depot at (0, 0). 3–4 towns at 15–60 km from the depot; customers drawn around town centres with a 3–6 km spread |
| Distance | Euclidean × 1.3 (detour factor) |
| Travel time | distance / 55 km/h, plus 4 minutes per leg (access), rounded up to an integer; add ±5 % asymmetric noise so asymmetric code paths are exercised |
| Customer types | grocery: window 06:00–10:00; restaurant: 08:30–11:30; site: 07:00–15:00. Tail-lift need drawn per type |
| Orders | each customer orders on a given weekday with a type-specific probability and a weekday factor (Monday and Thursday heavier); `pallets` = 1 + Poisson(λ_type), capped at the largest truck |
| Service time | `service_mu` = 10 + 6 × pallets, integer |
| Drivers (`small`) | 3 full-time (shift starts 05:30, 06:00, 07:00), 1 part-time (06:00); 4 trucks of 12–18 pallets, one with tail-lift |
| Validity | the generator must certify each day by producing at least one plan that passes the checker (the baseline, with postponements allowed). An instance that only certifies with more than 20 % postponed orders is regenerated |

Output: `data/instances/<name>/<seed>/week.json` plus `matrix.npz`.

### 5.2 Mode B — realistic geography (optional, task T10)

- Customers: SIRENE geolocated establishments (Licence Ouverte 2.0), joined on SIRET to the SIRENE stock to get the NAF code; filter by NAF (restaurants `56.10A`, food retail `47.11*`, construction `41.20*`, `43.*`) and by communes within a radius of a real logistics zone. Coordinates are Lambert-93; convert to WGS84 with `pyproj`.
- Matrices: OpenRouteService matrix endpoint (≤ 3 500 origin × destination pairs per request, i.e. 50 × 50) or a self-hosted OSRM; cache to `data/cache/matrix_<sha1 of node list>.npz`.
- Fetching is a separate command (`python -m legalvrp.data.fetch ...`). Generation then reads only the cache.

---

## 6. The daily MILP (implement exactly; copy into `docs/MODEL.md`)

### 6.1 Sets and parameters

- $C$: orders of the day, including orders postponed from the previous day.
- Nodes: $0$ (depot departure), $E$ (depot return), $C$.
- $K$: drivers working that day. $C_k \subseteq C$: orders compatible with driver $k$.
- $A_k$: arcs for driver $k$: $(0,j)$ for $j\in C_k$; $(i,j)$ for $i \neq j \in C_k$; $(i,E)$ for $i \in C_k$; $(0,E)$ (the empty route).
- **Arc pruning (mandatory):** drop $(i,j)$ if $e_i + s_i + \tau_{ij} > l_j$.
- $\tau_{ij}$ minutes, $d_{ij}$ km, $[e_i, l_i]$ service-start window, $s_i$ = `service_mu`, $q_i$ pallets, $Q_k$ capacity.
- $S_k$ shift start, $F_k$ shift end max; $P$ = `depot_prep`, $R$ = `depot_close`.
- Rules: $DB = 270$, $WB = 360$, $BR = 45$, $DD = 540$, $DS = 720$, $WD = 3360$.
- State: $W_k$ service minutes worked this week, $V_k$ driving minutes this week.
- Contract: $H^{\text{thr}}_k$, $H^{\max}_k$; costs $c^{\text{km}}$, $c^{\text{reg}}_k$, $c^{\text{ext}}_k$, $c^{\text{fix}}_k$, $p_i$.

### 6.2 Variables

| Name in code | Math | Domain | Meaning |
|---|---|---|---|
| `x[i,j,k]` | $x_{ijk}$ | binary | driver $k$ travels arc $(i,j)$ |
| `u[i]` | $u_i$ | binary | order $i$ postponed |
| `T[i,k]` | $T_{ik}$ | $[e_i, l_i]$ | service start at $i$ if $k$ visits it |
| `D[i,k]` | $D_{ik}$ | $[0, DD]$ | driving accumulated on arrival at $i$ |
| `y[i,k]` | $y_{ik}$ | binary | $k$ takes the break right after serving $i$ |
| `t0[k]` | $t^0_k$ | $[S_k + P,\ F_k]$ | departure from depot |
| `tE[k]` | $t^E_k$ | $[S_k + P,\ F_k - R]$ | return to depot |
| `a[k]` | $a_k$ | $[S_k, F_k]$ | break start |
| `b[k]` | $b_k$ | $[0, DD]$ | driving accumulated at the break |
| `svc[k]` | $\theta_k$ | $[0, DS]$ | *temps de service* today |
| `ext[k]` | $o_k$ | $\ge 0$ | weekly minutes above the contract threshold after today |

Expressions (not variables): $\text{visit}_{ik} = \sum_{(j,i)\in A_k} x_{jik}$; $\text{used}_k = 1 - x_{0Ek}$; $\text{drive}_k = \sum_{(i,j)\in A_k}\tau_{ij}x_{ijk}$; $\text{brk}_k = \sum_i y_{ik}$.

### 6.3 Objective

$$\min\ \sum_{k}\Big[c^{\text{km}}\!\!\sum_{(i,j)\in A_k}\! d_{ij}x_{ijk} + c^{\text{reg}}_k\,\theta_k + c^{\text{ext}}_k\,o_k + c^{\text{fix}}_k\,\text{used}_k\Big] + \sum_{i\in C} p_i\,u_i$$

### 6.4 Constraints

| # | Name in code | Constraint | For |
|---|---|---|---|
| C1 | `cover` | $\sum_{k: i\in C_k}\text{visit}_{ik} + u_i = 1$ | $i \in C$ |
| C2 | `leave` | $\sum_{j:(0,j)\in A_k} x_{0jk} = 1$ | $k$ |
| C3 | `flow` | $\sum_{j}x_{jik} = \sum_{j}x_{ijk}$ | $k,\ i\in C_k$ |
| C4 | `cap` | $\sum_{i}q_i\,\text{visit}_{ik} \le Q_k$ | $k$ |
| C5 | `time_dep` | $T_{jk} \ge t^0_k + \tau_{0j} - M^{0}_{jk}(1-x_{0jk})$ | $k,\ (0,j)$ |
| C6 | `time_arc` | $T_{jk} \ge T_{ik} + s_i + BR\,y_{ik} + \tau_{ij} - M_{ij}(1-x_{ijk})$ | $k,\ (i,j)$, $i,j\in C_k$ |
| C7 | `time_ret` | $t^E_k \ge T_{ik} + s_i + BR\,y_{ik} + \tau_{iE} - M^{E}_{ik}(1-x_{iEk})$; and $t^E_k \ge t^0_k$ | $k,\ i$ |
| C8 | `drv_dep` | $\tau_{0j} - DD(1-x_{0jk}) \le D_{jk} \le \tau_{0j} + DD(1-x_{0jk})$ | $k,\ (0,j)$ |
| C9 | `drv_arc` | $D_{ik} + \tau_{ij} - (DD+\tau_{ij})(1-x_{ijk}) \le D_{jk} \le D_{ik} + \tau_{ij} + DD(1-x_{ijk})$ | $k,\ (i,j)$ |
| C10 | `brk_site` | $y_{ik} \le \text{visit}_{ik}$; $\ \text{brk}_k \le 1$ | $k,\ i$ |
| C11 | `brk_time` | $T_{ik}+s_i - M^{a}_{ik}(1-y_{ik}) \le a_k \le T_{ik}+s_i + M^{a}_{ik}(1-y_{ik})$ | $k,\ i$ |
| C12 | `brk_drive` | $D_{ik} - DD(1-y_{ik}) \le b_k \le D_{ik} + DD(1-y_{ik})$; $\ b_k \le DD\,\text{brk}_k$ | $k,\ i$ |
| C13 | `drive_seg` | $b_k \le DB$; $\ \text{drive}_k - b_k \le DB$ | $k$ |
| C14 | `work_seg` | $a_k - (t^0_k - P) \le WB + M^{w}_k(1-\text{brk}_k)$; $\ (t^E_k + R) - (a_k + BR) \le WB + M^{w}_k(1-\text{brk}_k)$; $\ (t^E_k + R) - (t^0_k - P) \le WB + M^{w}_k\,\text{brk}_k$ | $k$ |
| C15 | `drive_day` | $\text{drive}_k \le DD$ | $k$ |
| C16 | `svc_def` | $\theta_k \ge (t^E_k + R) - (t^0_k - P) - BR\,\text{brk}_k - M^{w}_k(1-\text{used}_k)$ | $k$ |
| C17 | `week_caps` | $W_k + \theta_k \le H^{\max}_k$; $\ V_k + \text{drive}_k \le WD$ | $k$ |
| C18 | `extra` | $o_k \ge W_k + \theta_k - H^{\text{thr}}_k$ | $k$ |

**Why each piece is there** — keep these explanations in `docs/MODEL.md`:

- C6–C7 carry the break delay ($BR\,y_{ik}$) and also eliminate subtours, because every $s_i + \tau_{ij} > 0$. The generator must guarantee $s_i \ge 1$.
- C8–C9 are **equalities on used arcs**. With only the $\ge$ side, the solver could inflate $D_{ik}$ at the break node to make the after-break driving segment look shorter. Do not "simplify" them to one side.
- C11–C12 fix $a_k$ and $b_k$ to the values at the break node and are inactive elsewhere.
- C13 with $b_k = 0$ (no break) reduces to "a duty without a break drives ≤ 270".
- C14: the first two lines apply when there is a break, the third when there is not. Pre-trip $P$ and post-trip $R$ are work and belong to the first and last segment respectively; omitting $R$ accepts plans that are illegal by up to $R$ minutes.
- C16 makes an unused driver's service time 0, so unused drivers do not consume weekly hours.
- $\theta_k$ is bounded below only. It is never used as the reported value: **KPIs and the next day's state are recomputed from the extracted routes**, not read from model variables.

**Validation status.** This formulation, with the big-M values below, was checked against exhaustive enumeration on 42 random days ($n = 5$, $K = 2$) in three regimes (mixed, work-heavy, driving-heavy) with zero mismatches. Random instances alone did **not** detect a one-sided C9; the handcrafted trap of T6 does (illegal objective 742.00 against the correct 1 532.40). Treat the formulation as correct and the traps as mandatory.

### 6.5 Big-M values (compute per constraint; never use one global constant)

| Constraint | $M$ |
|---|---|
| C5 | $M^0_{jk} = \max(0,\ F_k + \tau_{0j} - e_j)$ |
| C6 | $M_{ij} = \max(0,\ l_i + s_i + BR + \tau_{ij} - e_j)$ |
| C7 | $M^E_{ik} = \max(0,\ l_i + s_i + BR + \tau_{iE} - S_k - P)$ |
| C11 | $M^a_{ik} = \max(F_k,\ l_i + s_i) - \min(S_k,\ e_i + s_i)$ |
| C14, C16 | $M^w_k = F_k - S_k + P + R$ |
| C8, C12, upper side of C9 | $DD$ |
| lower side of C9 | $DD + \tau_{ij}$ — with only $DD$, a visited node with high accumulated driving would wrongly constrain its successor through an *unused* arc |

### 6.6 Symmetry and warm start

- If two drivers are identical (same contract, truck capacity and tail-lift, shift, state), add $\sum_{i} \text{visit}_{ik} \ge \sum_{i} \text{visit}_{i,k+1}$ for consecutive identical drivers.
- Always pass the baseline plan (§7) as a MIP start through `Start` attributes on `x`, `y`, `u`, `t0`.

### 6.7 Gurobi usage

Parameters come from `config/solver.yaml`: `TimeLimit` (120 s per day for `small`), `MIPGap` (0.01), `Threads`, `Seed`, `OutputFlag`, `LogFile` (one log file per solve under `results/`). Name every variable and constraint with the code names above and their indices. For `tiny` instances, also write the model to `.lp`. If the status is `INFEASIBLE`, compute the IIS and write `.ilp` before raising. Record for every solve: status, runtime, objective, best bound, gap, node count, number of variables, constraints and non-zeros before and after presolve.

If the time limit is hit **with** an incumbent, accept it and report the gap. If it is hit **without** one, return the baseline plan and flag the day.

---

## 7. Baseline heuristic (`heuristics/territory.py`)

Represents current practice and provides the MIP start.

1. Cluster the day's orders into $|K|$ territories with k-means on coordinates, seeded.
2. Assign territories to drivers by a minimum-cost assignment (Hungarian, `scipy.optimize.linear_sum_assignment`) on centroid distance, respecting compatibility.
3. Route each territory by nearest neighbour from the depot, then 2-opt, keeping only moves the route evaluator accepts.
4. Orders that cannot be inserted legally anywhere are postponed.

**Route evaluator (`heuristics/route_eval.py`)**: given a driver and a sequence, for every break position plus "no break", find the departure time that minimises *temps de service* while keeping the route legal — departing later absorbs waiting, which is what the MILP does implicitly because $\theta_k$ has a cost. Scanning integer departure minutes from $S_k + P$ is exact and fast enough at V0 sizes; stop the scan when a window becomes infeasible or the duration stops decreasing. Return the cheapest legal option, or the list of violated rules. This evaluator is used by the heuristic only. The checker is separate (§8).

---

## 8. Independent checker (`check/checker.py`)

Written **from §3 and §6.4 in words**, not from `route_eval.py`. Inputs: a `DayInstance`, a `DayPlan`, the rules. It recomputes every time in each route from the plan's reported departure, sequence and break position, and returns a list of violations, each with rule key, driver, order and amount. It checks: each order served exactly once or postponed; compatibility; capacity; windows; departure ≥ shift start + prep; return + close ≤ shift end; driving before and after the break; work before and after the break; daily driving; daily service; weekly service and weekly driving against the state; reported times consistent with travel and service durations.

It also has a **week mode**: daily rest between consecutive duties and weekly totals.

**Test corpus (`tests/fixtures/duties.json`)**: at least 30 hand-built duties, each labelled legal or illegal with the expected rule key. Include boundary cases (exactly 270, 271 minutes of driving), the inflated-driving trap of C8–C9, a duty legal only if the break is placed late, and a duty legal with no break.

---

## 9. ML readiness without ML

- `Order.service_sigma` exists and is 0.
- `config/risk.yaml` has `enabled: false` and `z: 0.0`. The model ignores it in V0.
- `estimate/base.py` defines an `Estimator` protocol: given the day's orders and roster, return `service_mu` and `service_sigma` per order. The only V0 implementation is `DeterministicEstimator`, which returns the generator's values with sigma 0.
- The generator stores the *true* service time separately from the estimate (`truth.json`), even though they are equal in V0. V1 will make them differ.

Nothing else. Do not add a model, a training loop or a dependency for this.

---

## 10. Environment

| Item | Choice |
|---|---|
| Python | 3.12 |
| Environment manager | `uv` (or `venv` + `pip`); lock file committed |
| Solver | `gurobipy`, version pinned. Restricted pip licence for tests; academic licence for `small` and `scale` |
| Core libraries | `numpy`, `pandas`, `pydantic` (v2), `pyyaml`, `scipy` (assignment), `scikit-learn` (k-means), `matplotlib` |
| Optional (`data` extra, T10 only) | `pyarrow`, `pyproj`, `requests` |
| Quality | `pytest`, `ruff` (lint and format), `mypy` (strict on `domain/` and `check/`) |
| CI | GitHub Actions: ruff, mypy, pytest. Tests that build a MILP use only `tiny` instances so they run under the restricted licence |

### Repository structure

```
legalvrp/
├─ PROJECT_BRIEF.md           this file
├─ README.md                  user-facing summary and how to run
├─ pyproject.toml  uv.lock
├─ config/
│  ├─ rules.yaml              §3 table
│  ├─ contracts.yaml          §3 contracts
│  ├─ costs.yaml              €/km, postponement penalty
│  ├─ solver.yaml             Gurobi parameters
│  ├─ risk.yaml               enabled: false
│  └─ instance_tiny.yaml  instance_small.yaml  instance_scale.yaml
├─ src/legalvrp/
│  ├─ domain/        models.py  rules.py  compat.py
│  ├─ data/          synthetic.py  orders.py  roster.py  matrix.py  io.py  fetch.py  generate.py
│  ├─ estimate/      base.py  deterministic.py
│  ├─ heuristics/    route_eval.py  territory.py
│  ├─ model/         milp_day.py  bigm.py  solve.py  extract.py
│  ├─ week/          state.py  loop.py
│  ├─ check/         checker.py
│  ├─ kpi/           kpis.py  report.py
│  └─ experiments/   run_week.py  scaling.py
├─ tests/
│  ├─ fixtures/      duties.json  tiny_day.json
│  ├─ test_rules.py  test_generate.py  test_checker_corpus.py
│  ├─ test_route_eval.py  test_milp_tiny.py  test_milp_vs_bruteforce.py
│  └─ test_week_loop.py
├─ docs/
│  ├─ MODEL.md                §6, kept in sync with code
│  ├─ DATA.md                 §5, sources and licences
│  └─ DECISIONS.md            dated log of every modelling decision
├─ data/                      git-ignored (instances/, cache/, raw/)
└─ results/                   git-ignored (plans, logs, KPIs, figures)
```

Entry points (module CLIs): `python -m legalvrp.data.generate --config ... --seed ...`, `python -m legalvrp.experiments.run_week --instance ...`, `python -m legalvrp.experiments.scaling --config ...`, `python -m legalvrp.check.checker --instance ... --plan ...`.

---

## 11. Outputs and KPIs

Per day: `results/<run>/day<d>/plan.json`, `gurobi.log`, `stats.json`, `violations.json` (must be empty). Per week: `week_kpis.json` and `week_report.md`.

| KPI | Definition |
|---|---|
| `cost_total` | objective recomputed from routes |
| `km` | total distance |
| `drivers_used` | per day |
| `service_hours` | per driver per day and per week |
| `extra_hours` | weekly minutes above threshold per driver |
| `postponed` | orders postponed per day; orders unserved at Friday's end |
| `on_time_rate` | 1.0 in V0 by construction (deterministic); computed anyway |
| `hours_gini` | Gini coefficient of weekly service hours across full-time drivers |
| `solver` | runtime, gap, nodes, status per day |

Every KPI is computed from extracted routes via the checker's recomputation, never from model variables.

---

## 12. Tasks, in order

| # | Task | Acceptance criteria |
|---|---|---|
| T0 | Repository skeleton, `pyproject.toml`, configs with the §3 values, empty docs, CI running ruff + pytest | CI green on an empty test |
| T1 | `domain/` models, rules and contracts loaders with validation (units, ranges, required keys), compatibility function | Invalid configs fail with a message naming the key |
| T2 | Synthetic generator (mode A), JSON I/O, `tiny` and `small` fixtures | Same seed ⇒ byte-identical output; all $s_i \ge 1$; matrices integer |
| T3 | Route evaluator | Unit tests on hand-computed routes, including departure delay and best break position |
| T4 | Checker + duty corpus | All corpus labels reproduced; checker imports nothing from `model/`, `heuristics/`, `week/` (enforce with a test) |
| T5 | Territory baseline | Produces plans with zero checker violations on 20 generated days |
| T6 | Daily MILP builder per §6 with per-constraint big-M and arc pruning | `tiny` solves to optimality; plan passes the checker; **matches a brute-force optimum** (enumerate assignments, sequences, postponements, break positions and integer departure times via the route evaluator) on 10 random days with $n \le 5$, $K = 2$ in each of two regimes — *work-heavy* (service 45–80 min, customers 35–65 km out) and *driving-heavy* (service 10–20 min, customers 70–120 km out) — and on the **trap fixtures**: (a) *inflated driving*: one driver, three customers, legs depot→A 60, A→B 140, B→C 200, C→depot 140 min, wide windows, postponement penalty 1 000 € — serving all three is illegal because no break position splits driving into two parts ≤ 270, so the optimum must postpone; (b) *break delay*: a route feasible only if the 45 minutes are counted before the next arrival |
| T7 | Solve wrapper: parameters, MIP start from T5, extraction, stats, IIS on infeasibility | MILP objective ≤ baseline objective on every `small` day |
| T8 | Week loop: state update from extracted routes, postponement carry-over, daily-rest check, KPIs, report | Five days run end to end; week-mode checker returns zero violations |
| T9 | Scaling experiment: $n \in \{8,\dots,40\}$, 5 seeds, 300 s, records §6.7 statistics; plot runtime and gap against $n$ | Figure and a table of median runtime, median gap, share solved to optimality |
| T10 | *(optional)* Mode B geography with SIRENE + cached matrix | `small` week regenerated on real geography; everything above still passes |
| T11 | *(optional)* Clairvoyant weekly MILP (five daily blocks, weekly caps and threshold on totals) on reduced sizes | "Price of myopia" = rolling cost − clairvoyant cost, reported |

Stop after T9 (or T11). V1 — heuristics at scale and the ML duration layer — is a separate brief.

---

## 13. Things that will go wrong, and what to do

| Symptom | Likely cause | Action |
|---|---|---|
| Brute-force and MILP optima differ | one-sided driving propagation, wrong big-M, break delay missing in C7, or route evaluator not delaying departure | Compare the two plans with the checker; inspect the first diverging route |
| A driver with no orders has service time | C16 big-M term missing | Check `used_k` handling |
| Huge runtime on `small` | global big-M, missing arc pruning, no warm start | Fix §6.5 and §6.6 before tuning Gurobi |
| `INFEASIBLE` | a forced order (no postponement allowed) or inconsistent shift data | Read the IIS; postponement must always be available |
| Plan passes the model but fails the checker | the model and the checker disagree on a rule | The checker wins; fix the model and log the decision |
| Weekly state drifts | state updated from $\theta_k$ instead of recomputed routes | Recompute from routes |

---

## 14. Glossary

**Duty** — one driver's working day. **Temps de service** — French statutory service time: driving + other work + waiting, excluding breaks. **Threshold** — weekly hours above which extra pay applies. **Postponement** — moving an order to the next day at a penalty. **Rolling horizon** — solving day by day, passing state forward. **Clairvoyant** — solving the whole week at once with all orders known. **Arc pruning** — not creating variables for arcs that can never be used. **MIP start** — a feasible solution given to Gurobi before branch-and-bound.
