# Column generation lower bounds (V1-T9) — 2026-10-04

**Question.** ALNS finds very good plans (V1-T4), but how good? Above 25 orders the MILP's lower
bound after 300 s leaves 20–50 % uncertainty. Q6 asked for column generation as a tested
improvement: can it give **valid** lower bounds that certify ALNS much more tightly?

Code: `src/legalvrp/cg/`, tool `legalvrp-cg`. Data: [`cg/`](cg/).

## 1. Method (D-118, D-119)

**Master.** The LP relaxation of the set-partitioning model of V1-T3:

- one route per driver at most;
- each order served once or postponed (penalty);
- columns are **legal** routes, costed exactly by the fast evaluator;
- columns start from the ALNS route pool, so they are not invented.

**Why the bound is valid.** Pricing the legal routes exactly is very hard, because of breaks,
waiting before the break (D-007) and two work limits. A valid bound does not need it. After any
master solve, with duals π (π_i ≤ p_i at an LP optimum),

  LB = Σ_k idle_k + Σ_i π_i + Σ_k min over relaxed routes of k of (c̃ − Σ π)

is a lower bound on the day's optimum. This holds as long as the relaxed routes include every
legal route at a cost never above the true one: (π, μ = m̃) is then dual-feasible.

**Relaxed routes** (`cg/pricing`, exact forward labelling):

- The planners' break patterns are decisions after each service.
- Driving since the last qualifying break stays ≤ 4 h 30 at every stop.
- Times are optimistic: earliest service, each break as early as possible.
- Work stretches use the latest possible stretch start: a break may wait (D-007), and must end
  in time for the next window.
- ng-route memory (Baldacci, Mingozzi & Roberti 2011), with **dynamic neighbourhoods**
  (Martinelli, Pecin & Poggi 2014): the cycles of the best relaxed routes are forbidden and the
  pricing redone, which tightens the bound towards the elementary one where it matters.
- Dominance on (pattern, cost, ready time, load, driving, driving since the qualifying break,
  latest stretch start, ng memory).
- A completion bound prunes labels that cannot beat the best route found so far. The best gain
  per remaining minute is bounded by the best dual-per-minute ratio of any stop.

**Loop.**

1. A truncated labelling and best insertions into the LP's routes propose legal columns: the
   best relaxed routes are checked with the exact evaluator, or repaired by dropping one stop.
2. The exact labelling runs when they find nothing, and gives the bound.
3. On time, the bound of the last duals is computed on its own budget.
4. The master over the generated columns is then solved as a MIP, an upper bound that can
   improve on ALNS.

## 2. Verification

| check (`test_cg`) | result |
|---|---|
| relaxed cost ≤ true cost, every legal sequence of 20 regime days (both drivers) | holds |
| labelling = minimum of the relaxed cost over every sequence (elementary neighbourhoods) | equal on 40 driver-days; dynamic ng from size 1 reaches the same minimum |
| bound ≤ exact optimum (enumeration), 20 regime days | holds; **0.49 % below on average, equal on 18/20** |
| bound vs the exact master LP (all legal routes enumerated) | equal: the gap left is the LP's own |
| upper bound | a legal plan (checker), ≤ the ALNS cost |

Dynamic ng is what made the bound usable. On a 20-order `small` day, the bound with fixed
neighbourhoods of 8 was 8.4 % below the LP; with neighbourhoods of 20 (elementary) it was
exact but took 113 s. Dynamic neighbourhoods starting from 8 are exact in 7 s.

## 3. Results on the V1-T4 instances

`legalvrp-cg --config config/instance_scale.yaml --v0-rules --compare-csv docs/compare/compare.csv`.
These are the instances of V1-T4 under the same (V0) break rules, so that its 300-s MILP bounds
apply to the same problem. Column generation ran 120 s, plus at most 60 s for the final bound
and 30 s for the MIP. Seeds 1–5. Certified gap = (ALNS − bound) / ALNS. Raw data:
[`cg/cg_v0.csv`](cg/cg_v0.csv).

| n | valid CG bounds | ALNS certified by CG (median / max) | by the MILP bound (median / max) | by the better of the two (median / max) | CG bound > MILP bound | MIP over the columns beats ALNS |
|---|---|---|---|---|---|---|
| 8 | 5/5 | 0.0 % / 4.0 % | 1.0 % / 1.0 % | **0.0 % / 0.0 %** | 3/5 | 0/5 |
| 10 | 5/5 | 9.6 % / 17.5 % | 0.0 % / 0.9 % | 0.0 % / 0.9 % | 0/5 | 0/5 |
| 12 | 5/5 | 5.9 % / 12.5 % | 0.0 % / 0.7 % | 0.0 % / 0.7 % | 0/5 | 0/5 |
| 15 | 5/5 | 3.2 % / 30.3 % | 0.6 % / 28.5 % | 0.5 % / 28.5 % | 2/5 | 0/5 |
| 20 | 5/5 | 11.4 % / 47.5 % | 1.0 % / 15.1 % | 1.0 % / 13.2 % | 1/5 | 0/5 |
| 25 | 5/5 | 10.4 % / 17.5 % | 10.4 % / 20.2 % | **4.9 % / 17.5 %** | 3/5 | 0/5 |
| 30 | 5/5 | 11.2 % / 17.2 % | 13.1 % / 16.3 % | **11.2 % / 16.3 %** | 4/5 | 1/5 |
| 40 | 5/5 | **8.8 % / 11.9 %** | 23.7 % / 28.5 % | 8.8 % / 11.8 % | 4/5 | 2/5 |
| 60 | 3/5 | **6.8 % / 7.6 %** | 35.4 % / 44.9 % (3 runs) | 7.2 % / 44.9 % (4 runs) | 2/2 | 4/5 |
| 70 | 5/5 | **7.3 % / 8.3 %** | — | 7.3 % / 8.3 % | — | 3/5 |
| 80 | 3/5 | **8.6 % / 10.7 %** | 38.0 % / 52.7 % (3 runs) | 8.6 % / 10.7 % | 3/3 | 2/5 |
| 100 | 3/5 | **7.8 % / 10.1 %** | — | 7.8 % / 10.1 % | — | 3/5 |

- **From 25 orders on, column generation certifies ALNS several times more tightly than the
  MILP.** At 40–100 orders, ALNS is now proven within a median 7–9 % of the optimum, where the
  300-s MILP left 24–38 %. When both bounds exist, the CG bound is higher on 16 of 20 instances.
- **Up to 20 orders the MILP's bound is better**: branch and bound proves near-optimality there.
  There are two reasons, both tied to the end-of-horizon penalty (1 000 € per order left
  unserved) that dominates these one-day instances:
  - The LP relaxation of the route model has a large integrality gap. It covers hard orders
    with fractions of routes, and is up to 17 % below ALNS at 10 orders, even where the bound
    equals the LP.
  - On 4 of the 25 days with 15–20 orders, the bound stays below the LP even after convergence
    (by up to 47 %). The optimistic relaxation lets every driver "serve" an order that no legal
    route reaches, and that order's dual sits at the penalty.
- **No valid bound on 6 of the 20 largest days** (2 at 60, 2 at 80, 2 at 100): the exact pricing
  did not finish within the time budgets. These are reported as such, never estimated.
- **Upper bounds**: the MIP over the generated columns beats ALNS on 15 of 60 instances, all of
  them 30 orders or more, by up to 3.3 % (80 orders, seed 5). This is a small but real gain at
  scale, beyond the route pool of V1-T3.

## 4. Limits

- The bound is the LP's (Lagrangian). Closing the integrality gap of small, penalty-dominated
  days needs branch-and-price. That is not implemented: the MILP already proves these days.
- Exact pricing is the bottleneck above 60 orders (6 of 20 runs without a bound at 120 + 60 s).
  Known remedies, not implemented: bidirectional labelling, reduced-cost arc fixing,
  stabilisation of the duals.
- Single-threaded; drivers with identical templates share one pricing problem (4 problems for 12–20
  drivers here).
