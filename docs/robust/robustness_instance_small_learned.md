# Robustness to true service times, `instance_small.yaml`

Plans executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = learned estimator, mu + Z sigma per stop. Planned cost = the plan's weekly cost; realised cost = the same plans executed with `truth.json` (true weekly cost, checker in week mode).

| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| alns | v0 | 4527.08 | 4568.69 | 2.0 | 0.4 | 30.9 % | 19.6 % | 39.3 % |  | 7.8 % | 25.6 |
| alns | learned z=0.00 | 4787.71 | 4799.66 | 2.0 | 0.6 | 19.7 % | 13.4 % | 27.1 % | -31 % | 4.9 % | 11.4 |
| alns | learned z=0.50 | 5387.33 | 5391.10 | 2.8 | 1.2 | 10.5 % | 5.7 % | 13.6 % | -65 % | 2.5 % | -2.2 |
| alns | learned z=1.00 | 6503.99 | 6486.50 | 5.2 | 2.0 | 7.4 % | 3.4 % | 9.9 % | -75 % | 1.6 % | -12.3 |
| alns | learned z=1.50 | 8060.20 | 8035.95 | 7.2 | 3.4 | 3.0 % | 2.0 % | 4.7 % | -88 % | 0.7 % | -21.3 |
| alns | learned z=2.00 | 9755.25 | 9699.19 | 11.6 | 4.4 | 1.6 % | 1.1 % | 2.6 % | -93 % | 0.4 % | -30.3 |

Rules broken in reality (Monte Carlo, duties):

- alns | v0: daily_service_max 2; shift 1; weekly_service_max 153; work_after_break 303; work_before_break 1173; work_without_break 1996;
- alns | learned z=0.00: daily_service_max 1; weekly_service_max 239; work_after_break 87; work_before_break 1205; work_without_break 906;
- alns | learned z=0.50: daily_service_max 1; shift 1; weekly_service_max 17; work_after_break 66; work_before_break 687; work_without_break 271;
- alns | learned z=1.00: daily_service_max 1; shift 1; weekly_service_max 19; work_after_break 142; work_before_break 336; work_without_break 110;
- alns | learned z=1.50: daily_service_max 2; shift 3; weekly_service_max 19; work_after_break 157; work_before_break 128; work_without_break 59;
- alns | learned z=2.00: daily_service_max 2; shift 3; weekly_service_max 11; work_after_break 122; work_before_break 52; work_without_break 23;
