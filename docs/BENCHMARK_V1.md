# Benchmark V1 — 2026-10-04

## V1 at a glance

V1 (branch `v1`, release 1.0.0) adds large days, a metaheuristic, week policies, uncertain
service times, the law's break options, and certified bounds. Every plan of every experiment
passed the independent checker. Each line links to its report and data.

| task | result | report |
|---|---|---|
| T0 large instances | 60 / 70 / 80 orders a day over 5 days, certified, byte-identical on Windows and Linux | below, D-101 |
| T1 fast route evaluation | exact (0 mismatches on 100 000 cases, break patterns included), ≈ 57× faster than the STN | D-102, D-116 |
| T2 ALNS | exact on the regime days; ≤ the 300-s MILP from 25 orders; 60–100 orders within ~1 % of the best known in 30 s | below, D-103 |
| T3 route pool | never worse than its ALNS phase; no gain at equal time | below, D-104 |
| T4 scale benchmark | ALNS is the default day solver; MILP for ≤ 20 orders and proofs | below, D-105 |
| T5 week policies | planning today + tomorrow recovers 92 % of the cost of myopia; fairness weight 0.2 €/min balances full-time hours at no measurable cost | [POLICIES_V1](POLICIES_V1.md) |
| T6 uncertain service times | executed with true durations, 40–49 % of optimised duties become late or illegal (half of it estimation bias) | [ROBUSTNESS_V1](ROBUSTNESS_V1.md) |
| T7 learned estimator + robust planning | μ̂ + ½σ̂ per stop and a 15-min pooled reserve: −86 % / −88 % late or illegal duties for +15 % / +28 % cost | [ROBUST_V1](ROBUST_V1.md) |
| T8 break rules | 30-min break and 15 + 30 split in the checker and every planner; exact saving on 4/20 regime days (−8.4 %), MILP weeks −0.8 % | [BREAKS_V1](BREAKS_V1.md) |
| T9 column generation | valid bounds: ALNS certified within a median 7–9 % at 40–100 orders, against 24–38 % with the 300-s MILP bound | [CG_V1](CG_V1.md) |
| T10 release | ALNS bit-identical on every platform, with a regression on fixed seeds in CI (no Gurobi) | D-120, D-121 |

The rest of this page is the scale benchmark of T0–T4. Its section 1 is completed by T9's
certified gaps (section 5).

## The scale benchmark (V1-T0 … T4)


**Setup.** `legalvrp-compare --config config/instance_scale.yaml --orders 8 … 100 --milp-csv
docs/scaling/scaling.csv --milp-sizes 60 80`: the certified one-day instances of the V0-T9 scaling
experiment (same generator and seeds, so the V0 MILP results apply), extended to 60, 70, 80 and
100 orders a day (K = n/5 drivers), 5 seeds each — 60 instances. Methods:
baseline (territory), **ALNS** (V1-T2) for time T, **ALNS + pool** (V1-T3) for the same total T,
MILP (strong + connectivity cuts, 300 s: V0 runs for n ≤ 40, new runs for 60 and 80 on seeds 1–3).
T = 5 s (n ≤ 12), 15 s (n ≤ 20), 45 s (n ≥ 25). Machine: i7-13650HX, 14 cores.
Raw data: [`compare/compare.csv`](compare/compare.csv); figure:
[`compare/time_to_quality.svg`](compare/time_to_quality.svg). **Every plan of every method
passed the independent checker (0 violations on 60 instances × 4 methods).**

### 1. ALNS against the MILP

| n | K | ALNS ≤ MILP (300 s) | MILP gap to best known (median) | ALNS gap certified by the MILP bound (median / max) |
|---|---|---|---|---|
| 8–12 | 2–3 | **15/15** | 0 % | ≤ 1.0 % / 1.0 % |
| 15 | 3 | **5/5** | 0 % | 0.6 % / 28.5 % |
| 20 | 4 | **5/5** | 0 % | 1.0 % / 15.1 % |
| 25 | 5 | **5/5** | 0.03 % | 10.4 % / 20.2 % |
| 30 | 6 | **5/5** | 1.96 % | 13.1 % / 16.3 % |
| 40 | 8 | **5/5** | 10.7 % | 23.6 % / 28.5 % |
| 60 | 12 | **3/3** | 54.7 % | 35.4 % / 45.0 % |
| 80 | 16 | **3/3** | 96.1 % | 37.5 % / 52.7 % |

- **Up to 20 orders** ALNS finds the MILP's optimum on every instance (in < 1 s).
- **From 25 orders** ALNS (45 s) is at least as good as the MILP (300 s) on every instance, and the
  margin grows with n: at 60–80 orders the MILP barely improves on its baseline start (median
  55–96 % above the best known plan) while ALNS halves the baseline cost.
- The **certified gap** (ALNS vs the MILP's lower bound) grows from ≤ 1 % to 35–53 %. This is the
  weakness of the compact MILP's bound (V0), not evidence that ALNS is far from optimal: the bound
  stops being informative beyond ~25 orders. Column generation (V1-T9, section 5) now certifies
  ALNS within a median 7–9 % at 40–100 orders.

### 2. Time to quality (ALNS alone)

Median gap to the best known plan of each instance:

| n | 0.1 s | 0.5 s | 1 s | 2 s | 5 s | 10 s | 15 s | 30 s | 45 s |
|---|---|---|---|---|---|---|---|---|---|
| ≤ 20 | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % | | |
| 25 | 0.2 % | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % | 0 % |
| 30 | 0.5 % | 0.3 % | 0.2 % | 0.2 % | 0.1 % | 0 % | 0 % | 0 % | 0 % |
| 40 | 3.3 % | 2.0 % | 1.4 % | 0.7 % | 0.5 % | 0.4 % | 0.1 % | 0 % | 0 % |
| 60 | 6.5 % | 4.8 % | 3.8 % | 3.1 % | 2.0 % | 0.9 % | 0.6 % | 0.1 % | 0 % |
| 70 | 8.7 % | 4.4 % | 3.8 % | 3.4 % | 2.0 % | 1.8 % | 0.8 % | 0 % | 0 % |
| 80 | 10.6 % | 6.2 % | 5.7 % | 5.0 % | 3.5 % | 2.7 % | 2.0 % | 0.7 % | 0.7 % |
| 100 | 17.2 % | 7.6 % | 5.8 % | 5.1 % | 3.6 % | 2.6 % | 1.9 % | 0.3 % | 0.1 % |

A large day (60–80 orders) is within ~3 % in 5 s and within ~1 % in 30 s.

### 3. Does the route pool help? Not at equal time

ALNS + pool gets the same total time as ALNS alone (0.6 T ALNS, ≤ 0.3 T set partitioning,
0.1 T polish):

| n | pool better | pool worse | tie | mean difference |
|---|---|---|---|---|
| 8–40 | 0 | 0 | 40 | 0 % |
| 60 | 2 | 2 | 1 | +0.04 % |
| 70 | 3 | 2 | 0 | +0.12 % |
| 80 | 3 | 2 | 0 | −0.49 % |
| 100 | 3 | 2 | 0 | +0.18 % |

The pool always improves on *its own* ALNS phase (V1-T3), but the time it takes from ALNS cancels
the gain: **no systematic benefit** (52/60 better or equal, 8/60 worse, mean ±0.5 %). The routes
collected from one search are too similar to recombine into much better plans. Kept as an option;
the default day solver for large days is **ALNS alone**. A pool built from diversified searches (or
from column generation, V1-T9) may change this — to be measured, not assumed.

### 4. Frontier of each method

| Method | Use it for |
|---|---|
| MILP (strong + cuts) | ≤ 20 orders/day when a proof of optimality is wanted (V0); ground truth for tests |
| ALNS | every size; optimal-quality up to 40 orders in seconds; 60–100 orders within ~1 % in 30 s |
| ALNS + pool | no gain at equal time today; revisit with diversified columns |
| Territory baseline | reference only (50–220 % above the best plan from 15 orders up) |
| Column generation (V1-T9) | certificate of ALNS from 25 orders (bound valid on 54/60 instances); its MIP over the columns sometimes improves ALNS by up to 3 % |

### 5. Certified gaps with column generation (V1-T9)

Same instances (V0 break rules). Median certified gap of ALNS, with the better of the
column-generation and MILP-300 s bounds: ≤ 1 % up to 20 orders (the MILP's), 4.9 % at 25, 11.2 % at
30, 8.8 % at 40, 7.2 % at 60, 7.3 % at 70, 8.6 % at 80, 7.8 % at 100. Details and the cases where
each bound wins: [CG_V1.md](CG_V1.md).
