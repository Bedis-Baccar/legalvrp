# Learned service times and robust planning (V1-T7) — 2026-10-04

**Starting point** ([ROBUSTNESS_V1.md](ROBUSTNESS_V1.md)). Plans made with the rule
10 + 6 × pallets break when executed with the true stop times. 39 % (`small`) to 49 % (`medium`)
of the ALNS duties become late or illegal. About half of that comes from a biased estimate and
half from variance.

**Goal (V1_PLAN T7).** Learn the service times from history and plan with buffers. The share of
late or illegal duties must fall by **≥ 80 %**, and the cost must be reported.

Data: [`robust/`](robust/) (learned tables, curves as CSV and generated tables).

## 1. Learning from history (D-112)

`legalvrp-learn --config <instance> [--weeks 20]` builds a history of 20 generated weeks
(seeds 101–120, disjoint from the evaluation seeds 1–5). It keeps only the observed
(customer type, pallets, true minutes) of each stop and fits, per customer type:

- **mean** μ̂(p) = a + b·p, by least squares;
- **spread** σ̂(p) = cv·μ̂(p), with cv = RMS of (y / μ̂ − 1);
- **empirical quantiles** of y / μ̂ (0.5, 0.8, 0.9, 0.95). No distribution is assumed.

The result is exported as a table (type × pallets: μ̂, σ̂, quantiles) in JSON. Training is
offline and in C++, so no Python was needed (Q3). On `small` (1 792 stops), learned vs true
generator parameters:

| type | fixed | per pallet | cv |
|---|---|---|---|
| grocery | 12.1 / 12 | 5.83 / 6 | 0.287 / 0.30 |
| restaurant | 12.9 / 14 | 8.13 / 7 | 0.445 / 0.45 |
| site | 19.3 / 18 | 5.48 / 6 | 0.510 / 0.55 |

The fit is calibrated on 5 hold-out weeks:

- The planning rule covers only **43 %** of stops. It is optimistic: true mean 28.8 min against
  24.6 planned.
- The learned mean covers 58 % (lognormal skew).
- μ̂ + σ̂ covers 85 %, and the learned 0.9 quantile covers **90.2 %**.

The mean absolute error per stop barely moves (8.7 → 8.6 min). Single stop times are mostly
noise, so the gain is in removing the **bias**, not in predicting each stop.

## 2. Two ways to buffer (D-113)

Both act on the planning instance only. Every solver (exact evaluator, ALNS, MILP) and the
checker then plan with them unchanged. The realised day is always judged against the
**real** limits.

- **Per-stop buffer** (the plan's first idea): service minutes = μ̂ + z·σ̂ at every stop.
- **Pooled time reserve** R: keep R minutes free before every limit a delay can break
  (the 6-h work blocks, daily service, end of shift and of the daytime window, weekly cap) and
  before every window end (`estimate::reserve_time`). Delays of k independent stops add up
  like √k·σ, not k·σ. One reserve per duty therefore protects as well as a buffer at every
  stop, for much less lost capacity.

## 3. Cost-vs-risk curves (ALNS, 5 000 iterations/day, seeds 1–5, 200 scenarios/week)

Late or illegal duties (share of all duties) and the planned weekly cost (mean over the 5
weeks). The change is relative to the V0 plans.

**small**

| policy | late or illegal | vs V0 | planned cost | vs V0 |
|---|---|---|---|---|
| V0 rule | 39.3 % | — | 4 527 € | — |
| learned μ̂ (z = 0) | 27.1 % | −31 % | 4 788 € | +6 % |
| per-stop z = 1 | 9.9 % | −75 % | 6 504 € | +44 % |
| per-stop z = 1.5 | 4.7 % | −88 % | 8 060 € | +78 % |
| μ̂ + reserve 30 min | 4.7 % | −88 % | 5 704 € | +26 % |
| μ̂ + reserve 45 min | 1.6 % | −96 % | 5 779 € | +28 % |
| **μ̂ + ½σ̂ + reserve 15 min** | **4.8 %** | **−88 %** | **5 775 €** | **+28 %** |

**medium**

| policy | late or illegal | vs V0 | planned cost | vs V0 |
|---|---|---|---|---|
| V0 rule | 49.1 % | — | 7 898 € | — |
| learned μ̂ (z = 0) | 35.3 % | −28 % | 8 284 € | +5 % |
| per-stop z = 1 | 10.6 % | −78 % | 10 218 € | +29 % |
| per-stop z = 1.5 | 5.9 % | −88 % | 12 313 € | +56 % |
| μ̂ + reserve 30 min | 6.4 % | −87 % | 9 489 € | +20 % |
| μ̂ + reserve 45 min | 2.9 % | −94 % | 10 572 € | +34 % |
| **μ̂ + ½σ̂ + reserve 15 min** | **6.7 %** | **−86 %** | **9 054 €** | **+15 %** |

Full curves, with z ∈ {0, 0.5, 1, 1.5, 2} and R ∈ {0, 15, 30, 45, 60}, postponements, orders
unserved at the end of the week and the broken rules, are in [`robust/`](robust/).

- **Acceptance met**: −86 % to −88 % late or illegal duties.
- **Per-stop buffers alone are the expensive way** (2–3× the cost increase for the same
  protection), as the √k argument predicts.
- **Default (D-114)**: μ̂ + 0.5·σ̂ per stop and a 15-min reserve (`config/risk.yaml`). It meets
  the target on both instances at the lowest average cost (+15 % / +28 %).
- **Where the cost goes.** About 5–6 points of the increase are simply realism: the learned
  mean is longer than the rule. The rest is insurance, mostly through more postponements and
  orders left for the next day (small: 2.0 → 3.8 postponements per week).
- The "realised cost" column of the CSVs is the plan executed with `truth.json` (true weekly
  cost, checker in week mode). It stays close to the planned cost, because the cost function
  charges overruns only as overtime. Lateness and illegality have no price in the model, which
  is why they are reported as a separate risk axis.

## 4. With the MILP and the baseline

The buffers act on the planning instance, so they work with any solver. On `small`
(`legalvrp-robustness --solvers milp baseline --z 0.5 --reserve 15`; MILP 30 s/day):

| solver | policy | late or illegal | vs V0 | planned cost | vs V0 |
|---|---|---|---|---|---|
| MILP (V0 model) | V0 rule | 38.5 % | — | 4 344 € | — |
| MILP | μ̂ + ½σ̂ + reserve 15 min | 5.6 % | **−86 %** | 6 628 € | +53 % |
| baseline | V0 rule | 29.2 % | — | 7 095 € | — |
| baseline | μ̂ + ½σ̂ + reserve 15 min | 5.0 % | **−83 %** | 11 623 € | +64 % |

The reduction holds for every solver. The cost does not. The buffered instances are harder,
and the MILP limited to 30 s/day ends 15 % above ALNS on the same policy (6 628 € vs 5 775 €).
The baseline pays most, because it postpones whatever no longer fits its territories. Robust
planning therefore also argues for the default ALNS day solver (D-105).

## 5. Use

```
legalvrp-learn    --config config/instance_small.yaml                           # history -> results/estimators/instance_small.json
legalvrp-run-week --instance data/instances/small/1 --solver alns --estimator results/estimators/instance_small.json
legalvrp-robustness --config config/instance_small.yaml --estimator results/estimators/instance_small.json --z 0 0.5 --reserve 15 30
```

## 6. Limits

- Synthetic truth. The learned model fits it well by construction (linear mean, constant cv
  by type). Real stop times may need more features (customer, time of day, access); the table
  format and the `Estimator` interface take them without solver changes.
- One reserve value for every limit and window. A finer policy would size the reserve per
  duty from its own stops (√Σσ̂²), which is natural in the route evaluator but needs care in the
  MILP. It is left as a possible refinement.
- Lateness and illegality are reported, not priced. An operator would choose the point on the
  curve from its own cost of a late delivery or of an unplanned break.
