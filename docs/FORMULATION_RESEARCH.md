# Tighter formulations for the daily MILP — literature and choice for T6

Date: 2026-10-02. Question (review point F): the brief's model has one arc variable per
driver (three-index) and big-M time constraints. Its LP relaxation is known to be weak.
Is there a tighter formulation we can use in V0, and what does the state of the art do?

## 1. What the literature says

**State of the art for routing with driver rules is not a compact MILP.** The exact methods
that solve the vehicle routing and truck driver scheduling problem (VRPTW + hours-of-service)
are *branch-and-price(-and-cut)*: a set-partitioning master problem over routes, and a
pricing problem solved by labelling algorithms that build legal driver schedules.

- Goel & Irnich (2017) gave the first exact algorithm for the combined problem: branch-and-price,
  with route scheduling inside the resource-constrained shortest-path pricing.
- Tilk & Goel (2020) made the pricing bidirectional and added cuts. Their branch-and-price-and-cut
  covers US and EU hours-of-service rules and is significantly faster than one-directional labelling.
- Prescott-Gagnon et al. (2010) handled the EU driver rules inside a VRPTW heuristic.
- Zanella et al. (2025) review 41 papers (2000–2024) on routing under hours-of-service rules.

**For compact VRPTW formulations, the established strengthening techniques are:**

- *Time-window reduction and arc elimination* before solving (Desrochers, Desrosiers & Solomon
  1992; Kallehauge 2008). Windows are narrowed by cyclic rules using predecessors and successors,
  and impossible arcs are dropped. Narrower windows give smaller big-M values, hence a tighter LP.
- *Valid inequalities and branch-and-cut* (Bard, Kontoravdis & Yu 2002): subtour, capacity and
  path inequalities that cut off time-infeasible paths.
- *Extended time formulations*: the time-bucket formulation of Dash, Günlük, Lodi & Tramontani
  (2012) splits windows into buckets and gives much stronger LP bounds for the TSPTW. Mandal et al.
  (2024) propose compact VRPTW formulations restricted to locally elementary, time- and
  capacity-feasible flows, with a smaller integrality gap than the two-index baseline.

## 2. What we adopt for T6 (D-021)

The constraints are those of MODEL.md; only their form becomes tighter. Every change is either
provably valid or a preprocessing step, and T6 checks the result against brute force anyway.

| # | Change | Why it is valid | Why it helps |
|---|---|---|---|
| S1 | **Node-indexed time and driving variables**: `T[i]`, `D[i]` instead of `T[i,k]`, `D[i,k]`. C6 and C9 use the aggregated arc flow `X_ij = Σ_k x_ijk` and `Y_i = Σ_k y_ik`. C5, C7, C8, C11 and C12 keep the driver index where `t0`, `tE`, `a`, `b` are involved | Each order is served by at most one driver, so its service start and accumulated driving do not depend on k | `X_ij ≥ x_ijk`, so each big-M constraint is at least as tight as the K separate ones; time and driving variables drop from 2Kn to 2n |
| S2 | **Time-window reduction**: `e_i ← max(e_i, min_k(S_k + P) + τ_0i)`, `l_i ← min(l_i, max_k(F_k − R) − s_i − τ_i0)`, then the predecessor/successor rules of Desrochers et al. (1992), iterated until stable | A window point that no route can reach is never used | Smaller big-M in C5–C7 and C11, and more arcs pruned |
| S3 | **Extended arc pruning** (review F4): `τ_ij > DB`; per driver, `S_k + P + τ_0j > l_j`; `e_j + s_j + τ_jE + R > F_k`; `q_i + q_j > Q_k` | A break is taken only at a customer, so no leg can exceed 4 h 30 | Fewer binaries |
| S4 | **Tight big-M per constraint** recomputed from the reduced windows; C11 uses `max(F_k − e_i − s_i, l_i + s_i − S_k)` (review F5) | Same derivation as §6.5, with smaller ranges | Tighter LP |
| S5 | **Cheap valid inequalities**: `X_ij + X_ji ≤ 1`; `visit_ik ≤ used_k`; `Σ_i q_i visit_ik ≤ Q_k used_k`; `drive_k ≤ DB (1 + brk_k)`; `visit_ik ≤ brk_k` when the shortest depot→i→depot driving exceeds DB | Each follows from C2–C4, C13 and C15 | They cut off fractional points the big-M form allows |
| S6 | **Complete warm start** from the baseline (review F6) | — | A good incumbent from node 0 |

The brief's original formulation (§6, three-index) stays available as a **reference build
option**. T6 checks both against brute force; T9 reports both (root gap, runtime, share solved).
That measures the gain instead of assuming it.

## 3. What we do not adopt in V0, and the V1 path

- **Branch-and-price with labelling** is the real answer beyond about 25 orders a day. Our exact
  route evaluator (D-016) already gives legal schedules for a fixed sequence, so it is the natural
  feasibility check inside a labelling pricer. That is V1, together with ALNS. The brief puts
  column generation out of scope for V0.
- **Time-bucket / time-expanded formulations**: much tighter, but the model grows by about one
  variable per arc per bucket. Worth testing only if S1–S6 leave gaps on `small`.
- **Branch-and-cut on infeasible-path inequalities** through Gurobi callbacks: possible in T7 if
  the node count on `small` is high. Not needed before measuring.

## 4. Honest expectation

S1–S6 are standard and cheap, and they should make `small` (15–20 orders, 4 drivers) solvable to
near-optimality within the limit on most days. They do not change the fact that compact big-M
formulations scale poorly; T9 will show where this one stops.

## Sources

- Goel, A. & Irnich, S. (2017). An exact method for vehicle routing and truck driver scheduling
  problems. *Transportation Science* 51(2), 737–754. https://doi.org/10.1287/trsc.2016.0678
- Tilk, C. & Goel, A. (2020). Bidirectional labeling for solving vehicle routing and truck driver
  scheduling problems. *European Journal of Operational Research* 283, 108–124.
  https://doi.org/10.1016/j.ejor.2019.10.038
- Prescott-Gagnon, E., Desaulniers, G., Drexl, M. & Rousseau, L.-M. (2010). European driver rules
  in vehicle routing with time windows. *Transportation Science* 44(4), 455–473.
- Zanella, A. F. et al. (2025). Vehicle routing and scheduling under hours of service regulations:
  a review. *Transportation Research Part A* 201, 104665. https://doi.org/10.1016/j.tra.2025.104665
- Desrochers, M., Desrosiers, J. & Solomon, M. (1992). A new optimization algorithm for the
  vehicle routing problem with time windows. *Operations Research* 40(2), 342–354.
- Bard, J. F., Kontoravdis, G. & Yu, G. (2002). A branch-and-cut procedure for the vehicle routing
  problem with time windows. *Transportation Science* 36(2), 250–269.
- Kallehauge, B. (2008). Formulations and exact algorithms for the vehicle routing problem with
  time windows. *Computers & Operations Research* 35(7), 2307–2330.
- Dash, S., Günlük, O., Lodi, A. & Tramontani, A. (2012). A time bucket formulation for the
  traveling salesman problem with time windows. *INFORMS Journal on Computing* 24(1), 132–147.
  https://doi.org/10.1287/ijoc.1100.0432
- Mandal, U., Regan, A., Rousseau, L.-M. & Yarkony, J. (2024). A new class of compact formulations
  for vehicle routing problems. arXiv:2403.00262.

Legal sources for D-020: Code des transports art. L3312-1 (Légifrance); Directive 2002/15/EC art. 7.
