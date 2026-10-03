# Method comparison at scale (V1-T4)

One-day certified instances of the V0-T9 family (end-of-horizon penalties), K = ceil(n/5). ALNS alone and ALNS + pool get the same total time T (5 s for n <= 12, 15 s for n <= 20, 45 s beyond). MILP: strong formulation with connectivity cuts, 300 s (V0 run for n <= 40; new runs where marked). Gaps are relative to the best known solution of each instance; *certified gap* = (method - MILP lower bound) / method. Every plan passed the checker.

| n | K | runs | baseline gap | ALNS gap | ALNS + pool gap | MILP gap | ALNS <= MILP | ALNS certified gap (median / max) | MILP runs |
|---|---|---|---|---|---|---|---|---|---|
| 8 | 2 | 5 | 0.4% | 0.00% | 0.00% | 0.00% | 5/5 | 1.0% / 1.0% | 5 |
| 10 | 2 | 5 | 2.1% | 0.00% | 0.00% | 0.00% | 5/5 | 0.0% / 0.9% | 5 |
| 12 | 3 | 5 | 3.3% | 0.00% | 0.00% | 0.00% | 5/5 | 0.0% / 0.7% | 5 |
| 15 | 3 | 5 | 93.5% | 0.00% | 0.00% | 0.00% | 5/5 | 0.6% / 28.5% | 5 |
| 20 | 4 | 5 | 139.1% | 0.00% | 0.00% | 0.00% | 5/5 | 1.0% / 15.1% | 5 |
| 25 | 5 | 5 | 107.3% | 0.00% | 0.00% | 0.03% | 5/5 | 10.4% / 20.2% | 5 |
| 30 | 6 | 5 | 140.9% | 0.00% | 0.00% | 1.96% | 5/5 | 13.1% / 16.3% | 5 |
| 40 | 8 | 5 | 217.4% | 0.00% | 0.00% | 10.73% | 5/5 | 23.6% / 28.5% | 5 |
| 60 | 12 | 5 | 103.2% | 0.00% | 0.00% | 54.73% | 3/3 | 35.4% / 45.0% | 3 |
| 70 | 14 | 5 | 96.2% | 0.00% | 0.00% | — | — | — | 0 |
| 80 | 16 | 5 | 113.7% | 0.06% | 0.00% | 96.06% | 3/3 | 37.5% / 52.7% | 3 |
| 100 | 20 | 5 | 161.3% | 0.01% | 0.00% | — | — | — | 0 |

*ALNS <= MILP*: the better of ALNS and ALNS + pool is at least as good as the MILP's 300-s result. Time to quality: `time_to_quality.svg`. Raw data: `compare.csv`.
