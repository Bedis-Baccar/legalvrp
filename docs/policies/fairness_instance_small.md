# Fairness trade-off (V1-T5), `instance_small.yaml`

Rolling ALNS week (5000 iterations per day, deterministic), fairness weight in EUR per minute of (max - min) projected weekly service of the full-time drivers. Means over seeds; cost = true weekly cost; Gini and spread of the full-time drivers' weekly hours.

| weight (EUR/min) | cost (EUR) | vs weight 0 | Gini | spread (h) | overtime (min) | checker |
|---|---|---|---|---|---|---|
| 0 | 4527.08 | +0.00 % | 0.120 | 12.9 | 69 | OK |
| 0.02 | 4114.10 | +-9.12 % | 0.106 | 11.5 | 72 | OK |
| 0.05 | 4081.52 | +-9.84 % | 0.090 | 9.9 | 41 | OK |
| 0.1 | 4108.78 | +-9.24 % | 0.062 | 7.1 | 26 | OK |
| 0.2 | 4082.69 | +-9.82 % | 0.061 | 6.8 | 18 | OK |
| 0.5 | 4181.27 | +-7.64 % | 0.013 | 1.7 | 28 | OK |
| 1 | 4413.93 | +-2.50 % | 0.014 | 1.8 | 28 | OK |
