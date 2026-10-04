# Learned service-time estimator, `instance_medium.yaml`

Training: instance_medium.yaml, history seeds 101-120 (2799 stops). Hold-out: 5 further weeks (702 stops).

| type | n | fixed (learned / true) | per pallet (learned / true) | cv (learned / true) |
|---|---|---|---|---|
| grocery | 1216 | 11.85 / 12.00 | 6.11 / 6.00 | 0.285 / 0.300 |
| restaurant | 1180 | 14.27 / 14.00 | 6.42 / 7.00 | 0.436 / 0.450 |
| site | 403 | 19.08 / 18.00 | 5.48 / 6.00 | 0.504 / 0.550 |

**Calibration (hold-out)**: share of stops whose true minutes are <= the planned minutes

| planned minutes | share covered | mean planned (min) |
|---|---|---|
| planning rule 10 + 6 x pallets (V0) | 42.0 % | 24.1 |
| learned mu + 0.0 sigma | 58.7 % | 28.3 |
| learned mu + 0.5 sigma | 73.9 % | 33.6 |
| learned mu + 1.0 sigma | 85.3 % | 39.1 |
| learned mu + 1.5 sigma | 92.5 % | 44.3 |
| learned mu + 2.0 sigma | 95.9 % | 49.7 |
| learned empirical quantile 0.50 | 49.6 % | |
| learned empirical quantile 0.80 | 79.6 % | |
| learned empirical quantile 0.90 | 90.6 % | |
| learned empirical quantile 0.95 | 95.4 % | |

Mean absolute error per stop (hold-out): planning rule 8.9 min, learned mean 8.6 min.
