# Benchmark V0 — daily MILP vs territory baseline (2026-10-03)

Instances: `small` seed 1 (4 drivers, 15–20 orders/day) and `medium` seed 1 (6 drivers,
25–30 orders/day), both certified (D-027). Machine: Intel i7-13650HX (14 cores), 32 GB,
Gurobi 12.0.3, all threads. Tool: `legalvrp-bench` (raw outputs in `results/bench/`,
git-ignored). **Every plan reported below passed the independent checker; both full weeks
also pass the week-mode checker.**

## 1. Correctness (T6 acceptance)

| Test | Result |
|---|---|
| `tiny` (6 orders, 2 drivers): optimal, legal, equal to exact enumeration | ✅ both formulations |
| 10 random **work-heavy** days (n = 5, K = 2): MILP = enumeration | ✅ 10/10, both formulations |
| 10 random **driving-heavy** days: MILP = enumeration | ✅ 10/10, both formulations |
| Trap (a) inflated driving (full matrix): optimum cannot serve all three | ✅ |
| Trap (b) break delay: A then B feasible only without the 45 min | ✅ |
| Connectivity cuts and duty knapsacks keep MILP = enumeration (valid) | ✅ |

## 2. Formulations (part A: each day fresh, same baseline start, 60 s)

*root gap* = (best − LP relaxation) / best; *gap* = Gurobi's final optimality gap.

**small** (15–20 orders)

| day | n | reference: root gap / final gap | strong + cuts: root gap / final gap | strong + cuts status |
|---|---|---|---|---|
| 0 | 20 | 89.8 % / 75.9 % | 63.8 % / **7.0 %** | time limit |
| 1 | 19 | 91.8 % / 65.3 % | 63.5 % / **22.6 %** | time limit |
| 2 | 15 | 90.6 % / 52.5 % | 64.2 % / **0.9 %** | **optimal, 14 s** |
| 3 | 20 | 91.2 % / 80.0 % | 59.5 % / **20.4 %** | time limit |
| 4 | 15 | 91.0 % / 57.4 % | 36.9 % / **0.0 %** | **optimal, 10 s** |

**medium** (25–29 orders)

| day | n | reference final gap | strong + cuts final gap |
|---|---|---|---|
| 0 | 29 | 89.1 % | 35.6 % |
| 1 | 29 | 89.4 % | 27.5 % |
| 2 | 27 | 82.7 % | 18.7 % |
| 3 | 28 | 88.5 % | 28.5 % |
| 4 | 25 | 76.9 % | 12.6 % |

Ablation on `small` day 0 (20 orders, 120 s): strong without cuts 54 % (at 30 s) → aggregate
connectivity cuts 28.9 % → **per-driver connectivity cuts 5.0 %**. The duty knapsacks add
2–3 % to the root bound. MIPFocus 3 (bound) did not help (day 4: optimal in 131 s vs 97 s).

**Reading.** The brief's formulation cannot prove anything at these sizes (root gaps ~90 %,
final gaps 53–89 %) and on `medium` it barely improves on its start. The strengthened model
(D-021, D-030, D-034, D-035) proves optimality at 15 orders in 10–16 s and leaves 5–23 % at
20 orders in 60–120 s. On day 4 the optimum was already found at 14 s: the residual gap is
mostly the lower bound, not the plan.

## 3. Rolling week (part B: Monday–Friday, carry-over, weekly state, 120 s/day)

| | small baseline | small MILP | reduction | medium baseline | medium MILP | reduction |
|---|---|---|---|---|---|---|
| cost (€, recomputed by the checker) | 6 869.5 | **4 385.1** | **36.2 %** | 11 821.0 | **8 824.4** | **25.4 %** |
| km | 2 858.0 | 2 542.1 | 11.1 % | 4 567.3 | 4 597.7 | −0.7 % |
| postponement decisions | 6 | 1 | 83 % | 7 | 2 | 71 % |
| orders unserved at Friday end | 1 | **0** | | 2 | **0** | |
| driver-days used | 20 | 17 | 15 % | 29 | 27 | 7 % |
| service hours | 106.5 | 100.1 | 6 % | 167.7 | 167.1 | 0 % |
| overtime minutes | 82 | 72 | 12 % | 70 | 135 | −93 % |
| MILP status per day | | optimal ×2, gaps 5 / 23 / 31 % | | | gaps 15–38 % | |

Daily savings: `small` 13.6–62.2 %, `medium` 0.2–51.1 %. The large Friday savings come from
the end-of-week penalty (D-019): the baseline leaves orders unserved, the MILP does not.

## 4. Is it realistic?

On `small` the MILP plans look like regional pallet distribution:
5–7 stops per route, duties of 5.5–7.5 h, all drivers back by 13:30 (morning windows),
the 45-min break taken mid-route at a customer when the duty exceeds 6 h, no waiting at
customers (departures timed to the windows), about 120–160 km per driver-day, and 3 instead
of 4 drivers on light days. Full-time drivers work ≈ 6 h/day (≈ 30 h/week, below the 39 h
threshold): the fleet is generously sized for this demand, so overtime is rare and the
economic levers are km and postponements. On `medium` the MILP serves more orders than the
baseline (2 postponements instead of 7, none unserved), which costs a little more driving
time and overtime; the total is still 25 % cheaper.

## 5. Conclusions

1. **The model is right**: exact agreement with enumeration on 20 random days and both traps;
   every plan passes the independent checker.
2. **It pays off**: −36 % weekly cost on `small`, −25 % on `medium` against the territory
   baseline, mostly through fewer postponements and fewer driver-days.
3. **Tractability frontier of the compact MILP** (this machine, 120 s): optimal at 15
   orders/day; 5–31 % gap at 20; 15–38 % at 25–30, where on some days the MILP barely
   improves on its start (`medium` day 1: −0.2 %). This is the measured version of
   `docs/SIZING.md` §2.4 and the justification for V1 (branch-and-price with our exact route
   evaluator as the pricing feasibility check; ALNS for scale).
4. The brief's original formulation is not usable at these sizes; the strengthening is what
   makes the MILP useful.

## 6. Week report with KPIs (T8, `legalvrp-run-week`, `small` seed 1, 120 s/day)

| KPI | baseline | MILP |
|---|---|---|
| cost_total (€) | 6 869.46 | **4 385.10** |
| km | 2 858.0 | 2 542.1 |
| served / unserved at the end | 88 / 1 | 89 / 0 |
| on_time_rate | 1.000 | 1.000 |
| hours_gini (full-time drivers) | **0.046** | 0.101 |
| extra minutes above thresholds | 82 | 72 |

The MILP's workload is less even: it saves by idling the 07:00 full-time driver on light days
(18.9 h in the week) while the 06:00 part-time driver, whose start suits the 06:00–10:00
grocery windows, works 25.2 h and 72 min above its 24 h threshold. Each daily model sees only
its own day; the cost of this myopia is what optional T11 (clairvoyant week) would measure.
If balance matters to the owner, a fairness term or a minimum-hours rule would be a model
decision (logged as an open question, not implemented).

> **Note (D-042, added after T11 work).** The weekly costs in §3 and §6 are sums of the daily
> objectives, which re-count weekly overtime on each day after the threshold is crossed. With
> 72–135 overtime minutes per week at ≤ 0.45 €/min, the overstatement is at most a few tens of
> euros per week — the conclusions (−36 % / −25 %) are unchanged. `legalvrp-run-week` now reports
> the true weekly cost as `cost_total` and the old figure as `sum_daily_objectives`.
