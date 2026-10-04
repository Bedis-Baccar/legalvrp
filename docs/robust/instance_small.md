# Learned service-time estimator, `instance_small.yaml`

Training: instance_small.yaml, history seeds 101-120 (1792 stops). Hold-out: 5 further weeks (447 stops).

| type | n | fixed (learned / true) | per pallet (learned / true) | cv (learned / true) |
|---|---|---|---|---|
| grocery | 768 | 12.11 / 12.00 | 5.83 / 6.00 | 0.287 / 0.300 |
| restaurant | 766 | 12.90 / 14.00 | 8.13 / 7.00 | 0.445 / 0.450 |
| site | 258 | 19.26 / 18.00 | 5.48 / 6.00 | 0.510 / 0.550 |

**Calibration (hold-out)**: share of stops whose true minutes are <= the planned minutes

| planned minutes | share covered | mean planned (min) |
|---|---|---|
| planning rule 10 + 6 x pallets (V0) | 43.0 % | 24.6 |
| learned mu + 0.0 sigma | 57.5 % | 28.8 |
| learned mu + 0.5 sigma | 73.2 % | 34.3 |
| learned mu + 1.0 sigma | 85.2 % | 39.6 |
| learned mu + 1.5 sigma | 92.4 % | 45.1 |
| learned mu + 2.0 sigma | 96.0 % | 50.5 |
| learned empirical quantile 0.50 | 48.5 % | |
| learned empirical quantile 0.80 | 76.7 % | |
| learned empirical quantile 0.90 | 90.2 % | |
| learned empirical quantile 0.95 | 95.3 % | |

Mean absolute error per stop (hold-out): planning rule 8.7 min, learned mean 8.6 min.
