# V1 plan — scale, robustness, smarter weeks

Branch `v1`. Builds on everything V0 delivered and measured. Each task has acceptance criteria;
a task starts only when the previous one meets them (same discipline as V0).

## 1. What V0 tells us V1 must fix

| V0 finding (measured) | Consequence for V1 |
|---|---|
| Compact MILP: optimal ≤ 12 orders/day, frontier at **25 orders** (median gap 11 % at 300 s), 32 % at 40 ([SCALING_V0](SCALING_V0.md)) | Large days (60–80 orders, 12–16 drivers) need a **heuristic** that scales; the MILP stays as ground truth on small days |
| The plan is found early, the bound is weak (day 4: optimum at 14 s, proof at 97 s) | Good solutions are cheap; **bounds** need another method (column generation) if we want to certify quality at scale |
| Exact route evaluator (temporal network) is fast and verified on 400 routes + the checker | It becomes the **core oracle**: feasibility and cost of every move in the heuristic and every column in pricing |
| Price of myopia: median 5.6 %, **up to 45 %** when a scarce skill (tail-lift) sits with a capped part-timer ([MYOPIA_V0](MYOPIA_V0.md)) | Add **look-ahead**: protect capped / scarce-skill drivers, plan with tomorrow's known orders |
| Hours Gini 0.10 (MILP) vs 0.05 (baseline): cheaper weeks are less fair | Optional **fairness** term, measured as a KPI trade-off |
| Service times deterministic (σ = 0); `truth.json` = estimate | **Uncertainty layer**: noisy truth, learned estimates, safety buffers, evaluation by re-checking plans against the truth |
| Conservative legal simplifications (one unsplit break, waiting = work, no 10 h extension) | Relax the ones that matter, each verified by the checker first |

## 2. Scope

**In:** ALNS heuristic for the daily problem; route-pool set-partitioning recombination;
look-ahead and fairness for the week; stochastic service times with a learned estimator and
robust planning; selected legal relaxations; scale benchmark up to 80 orders/day (large = 60–80).

**Out (V2 or later):** multi-depot, intraday re-planning, driver–truck swaps, time-dependent
travel times, user interface, real-data fetching (the import path of V0 stays).

## 3. Architecture changes

```
                       ┌──────────── week policy (look-ahead, fairness) ─────────────┐
instance ─► baseline ─►│ day solver = ALNS (any size)  ──► route pool ──► SP-MIP      │─► checker ─► plan
                       │            └ MILP (≤ 20 orders: exact, as today)             │
                       └──────── estimator (μ, σ per stop) ─► robust buffers ─────────┘
```

- New module `legalvrp::alns` (depends on `domain`, `heuristics`): solution representation,
  destroy/repair operators, acceptance, adaptive weights.
- `heuristics/route_eval` gains **incremental evaluation**: cached forward/backward schedule data
  per route so an insertion is tested without re-running Bellman–Ford on the whole route
  (exact fallback to the full STN when the cache cannot decide).
- New `legalvrp::pool` (set partitioning over routes found by ALNS, solved with Gurobi).
- `legalvrp::week` gets a `Policy` interface (myopic, look-ahead, fairness-weighted).
- `legalvrp::estimate` gets real estimators; `data` generates noisy truth; a new
  `legalvrp::robust` re-checks plans against sampled truths.
- The checker stays the judge and stays independent.

## 4. Tasks

| # | Task | Acceptance criteria |
|---|---|---|
| V1-T0 | **Larger instances**: `instance_large.yaml` (60 / 70 / 80 orders/day, 12–16 drivers, one depot), certified by the baseline | Certified weeks for 3 sizes × 5 seeds; generator still byte-identical across platforms |
| V1-T1 | **Incremental route evaluation** (insertion / removal / 2-opt delta) on top of the STN | Same legality and cost as the full evaluator on 100 000 random moves; ≥ 10× faster per insertion test |
| V1-T2 | **ALNS for one day**: destroy (random, worst-cost, related/Shaw, route, time-window cluster), repair (greedy, regret-2/3, with break-aware insertion), simulated-annealing acceptance, adaptive operator weights, postponement as a "bank" of unrouted orders | (a) Never illegal (checker); (b) within 1 % of the MILP optimum on every V0 day it solved (n ≤ 20); (c) better than the MILP incumbent at the 300 s limit for n ≥ 25; (d) deterministic for a given seed |
| V1-T3 | **Route-pool set partitioning**: keep good routes found by ALNS, solve a set-partitioning MIP over them (+ postponement), re-inject | Improves the ALNS result or equals it on ≥ 90 % of days, ≤ 30 s extra |
| V1-T4 | **Scale benchmark** (V0-T9 style) up to 80 orders (+ stress point 100): ALNS vs MILP vs baseline, time-to-quality curves | Figures + table; documented frontier of each method |
| V1-T5 | **Week policies**: (i) look-ahead with tomorrow's known orders; (ii) protection of capped / scarce-skill drivers (soft daily budget derived from the remaining week); (iii) optional fairness term (hours Gini or max–min) | On the V0 myopia instances: recover ≥ 70 % of the clairvoyant gap on average, never worse than myopic by > 1 %; fairness trade-off curve (cost vs Gini) reported |
| V1-T6 | **Uncertain service times**: generator draws true durations from a skewed distribution by customer type and pallets (μ, σ known to the generator, `truth.json` differs from the plan); evaluation tool re-times a plan with sampled truths and counts late arrivals and legal violations | Reproducible truths per seed; for the deterministic V0 plans, report the share of duties that become late or illegal |
| V1-T7 | **Learned estimator + robust planning**: estimator fitted on generated history (per type × pallets: quantiles or a small gradient-boosted model trained offline, exported as tables), buffer `z·σ` per stop in the evaluator and the MILP; choose z by a cost-vs-risk curve | With learned estimates and buffers: late/illegal duties reduced by ≥ 80 % vs deterministic plans, at a reported cost increase |
| V1-T8 | **Legal relaxations** (each first in MODEL.md and the checker, then in evaluator/ALNS/MILP): (a) 30-min break when work is 6–9 h; (b) 15 + 30 split break (10 h extension dropped, Q4) | Checker corpus extended (≥ 15 new labelled duties); MILP = enumeration on the trap and regime tests; measured saving vs V0 rules |
| V1-T9 | *(optional)* **Column generation / branch-and-price** with the evaluator-based pricing (labelling with the STN as feasibility check) for lower bounds on 25–60 orders | Valid bounds on the T4 instances; gap of the ALNS solution certified |
| V1-T10 | **Release**: docs (V1 model, benchmarks), CI including a Gurobi-free ALNS regression on fixed seeds | CI green; `docs/BENCHMARK_V1.md` |

## 5. How the heuristic will be built (V1-T2 details)

- **Representation:** per driver an ordered list of stops + the evaluator's schedule; a bank of
  postponed orders with their penalties.
- **Cost:** exactly the V0 objective (km, regular time, overtime, fixed, postponement), computed by
  the evaluator, so ALNS and MILP results are directly comparable and the checker agrees.
- **Insertion:** for each (driver, position) test with the incremental evaluator; the break
  position and departure come out of the STN, so legality is exact, never approximated.
- **Destroy size:** 10–40 % of served orders, adaptive; **acceptance:** simulated annealing with
  a temperature calibrated on the initial solution; **stop:** time limit or no improvement.
- **Start:** territory baseline (V0) or the MILP incumbent when available.
- **Validation:** every new best solution is checked; the test suite compares ALNS with the exact
  enumeration on the V0 regime days and with the MILP on all V0 benchmark days.

## 6. Decisions (2026-10-03)

| # | Question | Decision | Effect on the plan |
|---|---|---|---|
| Q1 | Balanced workload | **A goal** | V1-T5 adds a fairness term to the objective (spread of full-time drivers' weekly hours, max − min) with a tunable weight; the cost-vs-Gini curve sets the default |
| Q2 | Tomorrow's orders known at planning time | **Yes** | Look-ahead policy (V1-T5): plan with today + tomorrow visible, commit only today |
| Q3 | Offline Python for training | **Allowed if needed** | V1-T7 starts with C++ quantile tables; a Python training step (exported tables) only if tables are not good enough |
| Q4 | Legal relaxations | **The most relevant** | Regional distribution rarely drives more than 9 h a day, so the 10 h extension is dropped. Kept: (a) 30-min break when work is 6–9 h, (b) 15 + 30 split break — both change break placement on most duties |
| Q5 | Size of "large" | **60–80 orders/day** | V1-T0 sizes 60 / 70 / 80 (12–16 drivers); V1-T4 benchmark up to 80, one stress point at 100 |
| Q6 | Column generation | **Yes, as a tested improvement** | V1-T9 kept; adopted only if it measurably improves bounds or solutions on the T4 instances |

## 7. Risks

| Risk | Mitigation |
|---|---|
| Incremental evaluation with a mid-route break is subtle | Exact fallback to the full STN; 100 000-move equivalence test (V1-T1) |
| ALNS quality hard to judge beyond 25 orders | Pool MIP bound-free comparisons, MILP incumbents at long limits, optional column-generation bounds |
| Uncertainty model is invented (no real durations) | Keep distributions in config; plan to calibrate on LaDe / Amazon data shapes (V0 SIZING §3) |
| Scope creep | Task order and acceptance criteria as in V0; V2 list for everything else |

## 8. Status (2026-10-04)

| Task | State | Evidence |
|---|---|---|
| V1-T0 large instances | done | 15/15 size × seed weeks certified; golden hash (cross-platform) — D-101 |
| V1-T1 fast exact evaluation | done | 0 mismatches on 100 000 cases; 48× faster — D-102 |
| V1-T2 ALNS | done | exact on 20/20 regime days; = MILP optimum where proven; ≤ MILP-300 s on every instance from 25 orders — D-103, D-105 |
| V1-T3 route pool | done, kept optional | never worse than its ALNS phase; no systematic gain at equal total time — D-104, D-105 |
| V1-T4 scale benchmark | done | `BENCHMARK_V1.md`: 60 instances 8–100 orders, all legal |
| V1-T5 week policies | done | look-ahead (today + tomorrow) recovers 92 % of the myopia gap, never worse; reserve optional; fairness default 0.2 €/min (Gini 0.14 → 0.04 on `small`, +0.03 %) — `POLICIES_V1.md`, D-106 … D-108 |
| V1-T6 uncertain service times | done | true durations in `truth.json` (lognormal by type); executed with them, 40–49 % of optimised duties become late or illegal (29–38 % for the baseline); half of it is estimation bias — `ROBUSTNESS_V1.md`, D-109 … D-111 |
| V1-T7 learned estimator + robust planning | done | estimator learned from history (calibrated); μ̂ + ½σ̂ per stop + 15-min pooled reserve: −88 % / −86 % late or illegal duties for +28 % / +15 % cost (small / medium); per-stop buffers alone cost 2–3× more — `ROBUST_V1.md`, D-112 … D-114 |
| V1-T8 legal relaxations | done | checker = the law (any break list), corpus 60 duties; 30-min break and 15 + 30 split in every planner (fast = STN on 100 000 cases, MILP = enumeration); exact day saving on 4/20 regime days (−8.4 %); MILP weeks −0.8 % on small — `BREAKS_V1.md`, D-115 … D-117 |
| V1-T9 column generation bounds | done | valid Lagrangian bound from an exactly priced relaxation (dynamic ng); ALNS certified within a median 7–9 % at 40–100 orders (MILP-300 s: 24–38 %); MIP over the columns improves ALNS on 15/60 — `CG_V1.md`, D-118, D-119 |
| V1-T10 release | to do | next |
