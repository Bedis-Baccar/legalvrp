# Fairness trade-off (V1-T5), `instance_medium.yaml`

Rolling ALNS week (5000 iterations per day, deterministic), fairness weight in EUR per minute of (max - min) projected weekly service of the full-time drivers. Means over seeds; cost = true weekly cost; Gini and spread of the full-time drivers' weekly hours.

| weight (EUR/min) | cost (EUR) | vs weight 0 | Gini | spread (h) | overtime (min) | checker |
|---|---|---|---|---|---|---|
| 0 | 7897.91 | +0.00 % | 0.032 | 4.7 | 86 | OK |
| 0.02 | 8114.85 | +2.75 % | 0.028 | 4.1 | 121 | OK |
| 0.05 | 7693.45 | +-2.59 % | 0.024 | 3.7 | 52 | OK |
| 0.1 | 7781.90 | +-1.47 % | 0.024 | 3.7 | 91 | OK |
| 0.2 | 7864.57 | +-0.42 % | 0.013 | 1.9 | 126 | OK |
| 0.5 | 7886.06 | +-0.15 % | 0.007 | 1.1 | 85 | OK |
| 1 | 7505.85 | +-4.96 % | 0.003 | 0.5 | 102 | OK |
