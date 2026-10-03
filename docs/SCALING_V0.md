# Scaling experiment V0 (task T9) — 2026-10-03

**Question.** Up to which size does the compact daily MILP solve exactly? (`docs/SIZING.md`
§2.4 gave expectations; this replaces them with measurements.)

**Setup.** `legalvrp-scaling --config config/instance_scale.yaml`. For n ∈ {8, 10, 12, 15, 20,
25, 30, 40} orders and seeds 1–5: one certified day (D-027, D-040) with K = ⌈n/5⌉ drivers,
geography and customer types of `small`. Solved end to end: territory baseline as a complete
MIP start → strong formulation with connectivity cuts (D-021, D-030, D-034, D-035) → checker.
TimeLimit 300 s, MIPGap 1 %, all 14 cores (i7-13650HX, 32 GB), Gurobi 12.0.3.
Raw data: [`scaling/scaling.csv`](scaling/scaling.csv); figures:
[`scaling/runtime.svg`](scaling/runtime.svg), [`scaling/gap.svg`](scaling/gap.svg).
**All 40 plans passed the independent checker.**

## Results

| n | K | median runtime (s) | median gap | max gap | solved to optimality (≤ 1 %) | median binaries |
|---|---|---|---|---|---|---|
| 8 | 2 | 0.1 | 1.0 % | 1.0 % | **5/5** | 149 |
| 10 | 2 | 0.4 | 0.0 % | 0.9 % | **5/5** | 202 |
| 12 | 3 | 0.8 | 0.0 % | 1.0 % | **5/5** | 432 |
| 15 | 3 | 4.6 | 0.6 % | 28.5 % | **4/5** | 636 |
| 20 | 4 | 101.9 | 1.0 % | 15.2 % | **4/5** | 1 294 |
| 25 | 5 | 300 (limit) | 11.0 % | 20.3 % | 1/5 | 2 622 |
| 30 | 6 | 300 (limit) | 14.5 % | 17.9 % | 0/5 | 4 176 |
| 40 | 8 | 300 (limit) | 32.3 % | 60.5 % | 0/5 | 9 784 |

## Reading

- **Frontier.** The median gap at 300 s first exceeds 1 % at **n = 25** (K = 5): that is the
  compact model's frontier on this machine (the brief's T9 definition). Up to n = 12 every day
  solves in under 6 s; at 15–20 orders 4 of 5 days solve, from 1 s to 191 s.
- **Variance.** Difficulty depends strongly on the instance, not only on n: at n = 15 one seed
  stays at 28.5 % while the others solve in 1–6 s; at n = 20 runtimes range from 2 s to the limit.
  Tight capacity (K = 3 for 15 orders, fewer drivers than `small`) makes some days much harder.
- **Model size** grows as K·n² (149 → 9 784 binaries); the gap grows sharply from n = 25 and
  explodes at n = 40 (up to 60 %), where the bound, not the plan, is the problem.
- **Compared with the expectation of `docs/SIZING.md` §2.4** ("optimal up to ~15, small gaps
  to 25, wrong tool beyond 40"): measured is slightly better at 15–20 (thanks to the cuts) and
  confirms persistent gaps from 25 and the need for another method beyond.

## Caveat: postponement penalties in this experiment

Each scale instance is a one-day horizon, so its only day is the *last* day and every
postponement costs the end-of-week penalty (1 000 €, D-019) instead of 200 €. Runtimes and gaps
above are valid for that setting; the "saving vs baseline" column of `scaling/summary.md`
(median 48–58 % from n = 15) is **inflated** by those penalties and should not be quoted as a
weekday saving (use `docs/BENCHMARK_V0.md`: −36 % on `small`, −25 % on `medium`, over a week).
For a regular-weekday measurement: `legalvrp-scaling --config config/instance_scale.yaml --regular-day`
(about 2 h). Not run yet.

## Consequence for V1

A compact MILP is the right tool for days up to ~20 orders per depot with 4 drivers. Beyond,
the plan is a heuristic (ALNS) for scale, and branch-and-price for bounds, with our exact route
evaluator (D-016) as the feasibility and cost oracle of both — the route-level work is already
done and verified.
