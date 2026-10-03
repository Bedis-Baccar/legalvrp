# Decision log

Dated, append-only. Any change to the model goes to `docs/MODEL.md` first, then code, then here.
Status: **accepted**, **proposed** (needs sign-off), **superseded**.

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-001 | 2026-10-02 | **Implementation language is C++20** (CMake, MSVC on Windows, GCC in CI) instead of Python + gurobipy. Overrides PROJECT_BRIEF §2 ("C++ out of scope") and §10 | Design choice: one codebase from V0 to the V1 heuristics (ALNS) where C++ speed matters | accepted |
| D-002 | 2026-10-02 | Dependencies, pinned in `cmake/Dependencies.cmake`: nlohmann/json 3.12.0 (SHA-256 pinned), yaml-cpp 0.8.0, CLI11 2.5.0, Catch2 3.8.1; Gurobi 12.0.3 C++ API (installed locally). They replace pydantic/pyyaml/pytest. scipy (Hungarian) and scikit-learn (k-means) are replaced by in-house `heuristics/assignment` and `heuristics/kmeans` (≈ 150 lines, no dependency) | Minimal, header-light, widely used. Fetched at configure time only, never while solving or testing | accepted |
| D-003 | 2026-10-02 | **Portable RNG** (`data/rng`): a fixed generator (SplitMix64/PCG) plus in-house uniform, Poisson and normal sampling. `std::*_distribution` is forbidden in the generator | `std` distributions are implementation-defined. MSVC and libstdc++ give different draws for the same seed, which would break T2's "same seed ⇒ byte-identical output" between Windows and CI | accepted |
| D-004 | 2026-10-02 | **CI runs without Gurobi** (`ci-linux` preset, `-DLEGALVRP_WITH_GUROBI=OFF`, tests labelled `unit`). MILP tests (label `gurobi`) run locally under the academic licence | The pip restricted licence covers gurobipy only, not the C++ API, and the academic licence is tied to one host. Supersedes the brief's "CI runs MILP tests on tiny" | accepted |
| D-010 | 2026-10-02 | **Gurobi is mandatory for every local build.** There is no local no-Gurobi preset. `build.ps1` fails fast without `GUROBI_HOME` or a licence (`GRB_LICENSE_FILE`, default `%USERPROFILE%\gurobi.lic`). A test checks that the licence is not size-restricted (> 2000 variables) | Project choice; `small`/`scale` need the full academic licence | accepted |
| D-005 | 2026-10-02 | Module boundaries are enforced **at link time**: one static library per module, `legalvrp::check` links `legalvrp::domain` only. `tests/unit/test_layering.cpp` scans `#include`s as a second guard | Stronger than an import test: a forbidden dependency is a build error | accepted |
| D-006 | 2026-10-02 | Scaling plots (T9): C++ writes CSV; figures come from a small optional script under `scripts/` | Keeps the C++ build free of plotting dependencies | accepted |
| D-007 | 2026-10-02 | Route evaluator (§7), brute force (T6) and checker (§8) must allow **waiting before the break node** (`T_b` anywhere in `[earliest, l_b]`). The checker validates the *reported* service starts rather than re-scheduling | Without it they reject plans the MILP legally produces. Counter-example in `docs/REVIEW_V0.md` F1. MILP unchanged | accepted (option A) |
| D-008 | 2026-10-02 | **No access restrictions.** Any truck can deliver any customer. Removed: `Customer.access_class`, `Truck.size_class`, and the access penalty in `service_mu` (now `10 + 6 × pallets`). Compatibility = tail-lift if needed ∧ `pallets ≤ capacity`. Brief §1, §4, §5.1, §6.6 edited accordingly. The per-leg 4 min (parking/manoeuvring) is unrelated and kept | Simplification by design. It also resolves review F2 (reversed access rule) | accepted |
| D-009 | 2026-10-02 | Extra arc pruning (review F4) and tighter C11 big-M (F5); complete MIP start (F6) | Tighter formulation and faster solves, valid under A7 | **proposed** |

## Decisions still open (from `docs/validation/model_validation.tex` §11)

D1 break model · D2 waiting as work · D3 cost of regular hours · D4 postponement
penalty (escalation?) · D5 part-time ceiling · D6 headline (rolling vs clairvoyant).
Current defaults in `config/`: D1 one break, D2 waiting = work, D3 tie-breaker 0.01 €/min,
D4 constant 200 €, D5 contract + 10 %, D6 rolling.

## T2 — instance generation (2026-10-02)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-011 | 2026-10-02 | **Order generation is count-first.** Day d gets `n_d = clamp(round(U{min..max} × weekday_factor[d]), min, max)` orders, placed on n_d distinct customers drawn without replacement with weight `order_weight` of their type. Brief §5.1 instead lets each customer order independently with probability p_type × factor, which only hits the 15–20 target on average | Guarantees the brief's orders-per-day range on every day, keeps Mon/Thu heavier, and is simpler than tuning probabilities | accepted |
| D-012 | 2026-10-02 | **Certification deferred to T5.** §5.1 asks the generator to certify each day with the baseline + checker (≤ 20 % postponed). Those come in T4–T5, so `WeekInstance.certified = false` until then; `max_postponed_share` is already in the config | Task order | accepted |
| D-013 | 2026-10-02 | Instance files are `week.json` + `matrix.json` + `truth.json` (no `.npz` in C++). Rules, contracts and costs are **embedded** in `week.json` | An instance stays reproducible if `config/` changes later; the checker needs nothing but the instance | accepted |
| D-014 | 2026-10-02 | Generator parameters chosen (illustrative): pallets λ grocery 2.0 / restaurant 0.5 / site 2.5; tail-lift need 15 % / 25 % / 0 %; order weight 1 / 1 / 0.6; type shares 40 / 40 / 20 %; customer pool = 2 × max orders per day. `small` gives ≈ 44 pallets/day against 60 pallets of fleet | The brief leaves these open; tuned so the fleet is loaded but not overloaded. To revisit when T5 certification measures postponements | accepted |
| D-015 | 2026-10-02 | Cross-platform byte identity is enforced by **golden fixtures** (`tests/fixtures/instances/{tiny,small}/1`), regenerated by the test on every build and compared byte for byte, and by RNG golden values from an independent implementation | MSVC and GCC verified identical locally; Linux CI repeats the check | accepted |

## T3 — route evaluator (2026-10-02)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-016 | 2026-10-02 | **The route evaluator solves a simple temporal network** instead of scanning departure minutes (brief §7). For a fixed sequence and break position, every timing rule (travel + service, windows, shift, the three 6 h work segments, daily and weekly service caps) is a difference constraint `x_v − x_u ≤ c`. Bellman–Ford gives feasibility, the minimum `tE − t0` (so the minimum temps de service, so the minimum cost), and an integer schedule achieving it. Driving limits are time-independent and checked directly. If no break position is legal, the evaluator reports the most nearly legal option's violations, by adding constraint families one at a time | Exact and polynomial (≈ 10⁵ operations per route); handles D-007 (waiting before the break) with no special case; the brief's departure scan alone cannot. Verified against a naive brute force (every departure minute × every break-node start) on 400 generated routes: 177 legal, all equal | accepted |
| D-017 | 2026-10-02 | Rule keys shared by the evaluator and the checker live in `domain/violation.hpp`. `make_day_instance` (week → day) lives in `domain/day.hpp` | Shared vocabulary only, no logic, so the checker stays independent of `heuristics/` | accepted |

## Decisions after the critical review (2026-10-02)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-018 | 2026-10-02 | **Fixed duty start.** A used driver's duty runs from the shift start `S_k` to `tE + R`; waiting at the depot is work. C14 and C16 use `S_k` instead of `t0 − P`; `M^w_k = F_k − S_k`. The evaluator minimises the return time, then leaves the depot as late as possible | Salaried drivers come in at their shift time; the floating start under-counted work (review A) | accepted |
| D-019 | 2026-10-02 | **Escalating postponement penalty.** `p = 200 × 2^(days carried)`; on the last day at least 1 000 € (`unserved_end_penalty`). Applied in `make_day_instance`; the week loop must carry the original orders | A constant 200 € made remote orders cheaper to postpone than serve, indefinitely, and Friday leftovers free (review B) | accepted |
| D-020 | 2026-10-02 | **Daytime duties only, 05:00–19:00** (`earliest_duty_start`, `latest_duty_end`); `F_k = min(S_k + 765, 19:00)`. Night-work rules (Code des transports L3312-1: 10 h cap if work between 00:00 and 05:00 or ≥ 50 h/month in 21:00–06:00) cannot apply, so no night constraint is modelled. The config rejects a window starting before 05:00 or ending after 21:00 | Kept simple: no night work by design. The 05:30 driver was legal under the 12 h cap anyway (30 min/day of night ≈ 11 h/month) | accepted |
| D-021 | 2026-10-02 | **Strengthened compact formulation for T6** (S1–S6 in `docs/FORMULATION_RESEARCH.md`): node-indexed `T`, `D` with aggregated arc flow; window reduction; extended pruning; per-constraint big-M from reduced windows; cheap valid inequalities; complete warm start. The brief's §6 model is kept as a reference build option; both are checked against brute force, and T9 compares them | Review F: the three-index big-M model has a weak LP bound. These are standard, provably valid strengthenings (Desrochers et al. 1992; Kallehauge 2008); branch-and-price (Goel & Irnich 2017; Tilk & Goel 2020) is the V1 path | accepted, verified in T6 |
| D-022 | 2026-10-02 | **One trip per duty** made explicit in MODEL.md (no reloading at the depot) | It was implicit in A1 | accepted |

## T4 — independent checker (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-023 | 2026-10-03 | The checker validates the plan **as reported**: arrivals and return must equal departure + travel + service (+ break), service may start later than arrival (waiting) but never earlier. Work and service time are measured from the fixed shift start (D-018). It recomputes per driver: temps de service, driving, km and the §6.3 cost, plus postponement cost with effective penalties (D-019). KPIs and weekly state must use these recomputed facts | D-007 (the plan is what the driver does); brief §11 (never read model variables) | accepted |
| D-024 | 2026-10-03 | Week mode rebuilds each day with `make_day_instance`, carrying the **original** postponed orders, updates weekly state from recomputed facts, and checks the 11 h daily rest. Orders still postponed after the last day are reported as unserved (a cost, not a violation) | Brief §8 week mode | accepted |
| D-025 | 2026-10-03 | Checker validation: 41-duty labelled corpus (`tests/fixtures/duties.json`, exact rule-set match), plan- and week-level tests, agreement with the route evaluator on 300 generated routes (same facts and cost), and two mutation tests run by hand (a flipped label; dropping the post-trip close R): both caught | The checker is the judge; it must be shown to reject what it should | accepted |

## T5 — territory baseline and certification (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-026 | 2026-10-03 | **Territory→driver assignment cost = the actual cost of routing that territory with that driver** (nearest neighbour + repair + 2-opt through the exact evaluator) plus the penalties of the orders it would drop. Hungarian on that K×K matrix. The brief says "centroid distance" | Every driver starts at the same depot, so centroid distance is identical for all drivers and decides nothing; the route cost accounts for tail-lift, capacity, shift times and weekly hours | accepted |
| D-027 | 2026-10-03 | **Certification plays the baseline over the rolling week** (carried orders, weekly state from the checker's facts) and requires zero week-mode violations and ≤ `max_postponed_share` postponed on every day. If it fails, the generator is re-run with `attempt + 1` (independent derived seed); `week.json` records `certified` and `attempt` | Brief §5.1 Validity, applied the way the week is actually run. With the D-014 parameters, `small` seeds 1–6 certify within 2 attempts with 0–20 % postponed per day | accepted |
| D-028 | 2026-10-03 | k-means uses a deterministic farthest-first initialisation (no RNG); the baseline is fully deterministic | Reproducible on every platform without threading an RNG into `heuristics` | accepted |
| D-029 | 2026-10-03 | Leftover orders (dropped, incompatible, unassigned) are inserted at the cheapest legal position over all routes only if that costs less than their postponement penalty; otherwise postponed. Repair drops, at each step, the order whose removal gives the cheapest legal route; if none, the largest detour | Brief §7 step 4, made cost-aware so the baseline is a sensible MIP start | accepted |

## T6 — daily MILP (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-030 | 2026-10-03 | **Connectivity cuts** as Gurobi user cuts (`model/cuts`): for a customer set S and i in S, `Σ_{a∉S,b∈S} Σ_k x[a,b,k] ≥ Σ_k visit[i,k]`, separated on fractional nodes by max-flow depot → i (Edmonds–Karp), `PreCrush = 1`. On by default (`MilpOptions::connectivity_cuts`) | The first benchmark showed root gaps of 38–65 % (strong) and 89–92 % (reference): the LP serves remote orders through small fractional cycles. Integer solutions already satisfy the cuts (C6–C7 exclude subtours), so they only tighten the relaxation (Bard, Kontoravdis & Yu 2002; Kallehauge 2008) | accepted; effect measured in the benchmark |
| D-031 | 2026-10-03 | **One day end to end** (`week/solve_day`): baseline → MILP with a complete MIP start → extraction (departure and service starts from the model, arrivals and return recomputed) → checker; if it fails, the MILP sequences are re-timed by the exact evaluator and checked again; if that fails or there is no incumbent, the baseline is returned and flagged. The reported objective is the checker's recomputation | Brief §0.4 (the checker is the judge) and §6.7 (fallback) | accepted |
| D-032 | 2026-10-03 | **Exact enumeration** (`heuristics/enumerate`) is the T6 ground truth: best legal sequence per (driver, subset) by permutation + exact evaluator, then a DP over disjoint subsets with postponement. Trap (a) uses a full matrix in which every other leg is 300 min (> 270, never legal), closing review F3 | Exact for the model of MODEL.md; n ≤ 8 | accepted |
| D-033 | 2026-10-03 | Additional **medium** instance (`instance_medium.yaml`: 25–30 orders a day, 6 drivers including one part-time and one temp-agency) and the `legalvrp-bench` tool (A: formulations on fresh days; B: rolling week MILP vs baseline) | Test beyond the brief's sizes to judge realism and effectiveness | accepted |
| D-034 | 2026-10-03 | **Duty knapsacks** (strong formulation): per driver, `drive + Σ s_i visit_i + (P+R) used ≤ WB (used + brk)` (each work segment ≤ 6 h), `drive + Σ s_i visit_i + BR brk + (P+R) used ≤ (F − S) used` (the duty fits the shift), `svc ≥ drive + Σ s_i visit_i + (P+R) used` (temps de service ≥ work done) | The LP ignored the drivers' working-time budget (big-M time constraints vanish in the relaxation). Valid for every legal route; checked against enumeration. Measured: small gain (root LP +2–3 %) | accepted |
| D-035 | 2026-10-03 | **Per-driver connectivity cuts** added to D-030: `Σ_{a∉S,b∈S} x[a,b,k] ≥ visit[i,k]` for each driver k, separated on the first 1 000 nodes (aggregate cuts everywhere). Default on | The aggregate cut lets one driver's inflow cover another driver's fractional cycle. Measured on `small` day 0 (20 orders, 120 s): final gap 28.9 % → **5.0 %**; day 3: 27.7 % → 18.6 %; callback time ≤ 9 s of 120 s | accepted |

## T7 — solve wrapper (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-036 | 2026-10-03 | Per-day outputs (brief §11) by `week::write_day_outputs`: `plan.json` (validated plan), `stats.json` (§6.7 statistics + source, recomputed and baseline objectives, model size, cuts, start accepted), `violations.json` (empty), `gurobi.log` (via `log_dir` + tag). `MilpOptions::warm_start` can disable the MIP start (tests of the no-incumbent fallback). `MIPFocus` is configurable in `solver.yaml` | Brief §6.7, §11 | accepted |
| D-037 | 2026-10-03 | Presolve statistics are best effort: Gurobi's `presolve()` throws on a model it proves infeasible, which used to pre-empt the IIS. Now the stats are −1 and the solve reports infeasibility and writes the `.ilp` | Found by the T7 IIS test | accepted |

## T8 — week loop, KPIs, report (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-038 | 2026-10-03 | `week/state`: one place for the weekly state update (`update_states`, from the checker's facts only) and the carry-over (`carried_orders`: original orders with their first due day). Used by `run_week`, hence by certification and the CLI | Brief §13 ("state drifts if updated from θ"); no duplicated logic | accepted |
| D-039 | 2026-10-03 | KPIs (`kpi/kpis`) only from the checker's recomputation: `cost_total`, `km`, `drivers_used`, service hours per driver per day/week, extra minutes above thresholds, postponements per day, unserved at the end, `on_time_rate` (service start inside the window; 1.0 by construction in V0), `hours_gini` over full-time drivers (Σ\|x_i−x_j\| / 2n²·mean), solver statistics per day. `legalvrp-run-week` writes `results/<run>/day<d>/{plan,stats,violations}.json + gurobi.log`, `week_kpis.json`, `week_report.md`; exit code 0 only with zero week-mode violations | Brief §11 | accepted |

## T9 — scaling experiment (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-040 | 2026-10-03 | Scale instances derive from `instance_small.yaml` (geography, customer types, contracts): one day, exactly n orders, K = ⌈n/5⌉ drivers with driver and truck templates cycled; each is certified (D-027) before solving. `legalvrp-scaling` solves every (n, seed) end to end (baseline start → strong MILP + connectivity cuts → checker), TimeLimit 300 s, MIPGap 1 %, writes `scaling.csv` (flushed per row), `summary.md` and SVG figures generated in C++ | Brief T9; no plotting dependency (no Python toolchain on the machine), CSV stays the data of record | accepted |
| D-041 | 2026-10-03 | Scaling results recorded in `docs/SCALING_V0.md` (+ CSV and SVG in `docs/scaling/`): frontier n = 25 (median gap > 1 % at 300 s). The experiment used end-of-horizon penalties (one-day instances = last day); `--regular-day` added to measure regular weekdays | Brief T9; honest reporting of a methodological artefact found after the run | accepted |

## T10 / T11 — keep only what pays (2026-10-03)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-042 | 2026-10-03 | **KPI fix: `cost_total` is the true weekly cost** (routes + regular time + fixed + overtime on the FINAL weekly hours + postponement penalties). The former definition (sum of daily objectives, kept as `sum_daily_objectives`) counted overtime several times, because each day's objective includes the week's excess so far (brief §6.3) | Found while specifying T11; needed to compare rolling and clairvoyant weeks. Effect on earlier reports: only weeks with overtime, by at most the re-counted minutes × c^ext | accepted |
| D-043 | 2026-10-03 | **T10 reduced to an import path**: no fetching, no new dependency; `docs/DATA.md` documents how to write `week.json` + `matrix.json` from real data (SIRENE, OSRM/ORS) | Benefits (demo realism) do not outweigh the costs (network, an external API key, GB downloads, HTTP + PROJ dependencies) and would not change any conclusion | accepted |
| D-044 | 2026-10-03 | **T11 implemented on reduced instances**: `model/clairvoyant` (the five daily models — weekly parts of `MilpModel` — in one Gurobi model, linked by "served once", the rolling penalty accounting, weekly caps and overtime on weekly totals; no symmetry rule across days; rolling plans as MIP start) and `legalvrp-myopia` on `instance_myopia.yaml` (4–6 orders/day, a part-time and a full-time driver) | Measures the cost of the day-by-day myopia seen in T8 (part-time overtime, uneven hours). Verified on a hand-built week: rolling 869.40 vs clairvoyant 595.20, and model objective = checker's true cost | accepted |
