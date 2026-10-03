# Scaling experiment (T9)

Formulation `strong` with connectivity cuts, TimeLimit 300 s, MIPGap 0.01, Threads 0 (0 = all). One certified day per (n, seed); K = ceil(n/5) drivers. Postponement penalty: end-of-horizon (one-day instance = last day, D-019). Every reported plan passed the checker.

| n | K | runs | median runtime (s) | median gap | max gap | solved to optimality | median binaries | median vars / constrs (after presolve) | median saving vs baseline |
|---|---|---|---|---|---|---|---|---|---|
| 8 | 2 | 5 | 0.1 | 1.0% | 1.0% | 5/5 | 149 | 172 / 347 | 0.4% |
| 10 | 2 | 5 | 0.4 | 0.0% | 0.9% | 5/5 | 202 | 226 / 478 | 2.1% |
| 12 | 3 | 5 | 0.8 | 0.0% | 1.0% | 5/5 | 432 | 437 / 727 | 2.9% |
| 15 | 3 | 5 | 4.6 | 0.6% | 28.5% | 4/5 | 636 | 676 / 1063 | 48.3% |
| 20 | 4 | 5 | 101.9 | 1.0% | 15.2% | 4/5 | 1294 | 1352 / 1827 | 58.2% |
| 25 | 5 | 5 | 300.2 | 11.0% | 20.3% | 1/5 | 2622 | 2702 / 2918 | 51.8% |
| 30 | 6 | 5 | 300.4 | 14.5% | 17.9% | 0/5 | 4176 | 4272 / 4120 | 58.2% |
| 40 | 8 | 5 | 300.2 | 32.3% | 60.5% | 0/5 | 9784 | 9912 / 7387 | 56.2% |

*Solved to optimality* = Gurobi status OPTIMAL (gap <= MIPGap). Figures: `runtime.svg`, `gap.svg`.
