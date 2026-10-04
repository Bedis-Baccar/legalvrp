# Robustness to true service times, `instance_small.yaml`

Plans executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = learned estimator, mu + Z sigma per stop; `+RN` = also a pooled time reserve of N minutes before every limit and window end (the plan is judged against the real limits). Planned cost = the plan's weekly cost; realised cost = the same plans executed with `truth.json` (true weekly cost, checker in week mode).

| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| alns | v0 | 4527.08 | 4568.69 | 2.0 | 0.4 | 30.9 % | 19.6 % | 39.3 % |  | 7.8 % | 25.6 |
| alns | learned z=0.00 | 4787.71 | 4799.66 | 2.0 | 0.6 | 19.7 % | 13.4 % | 27.1 % | -31 % | 4.9 % | 11.4 |
| alns | learned z=0.00 +R15 | 5247.94 | 5260.61 | 3.2 | 1.0 | 9.0 % | 5.5 % | 12.1 % | -69 % | 2.2 % | 11.5 |
| alns | learned z=0.00 +R30 | 5703.76 | 5723.45 | 4.2 | 1.2 | 3.3 % | 2.6 % | 4.7 % | -88 % | 0.8 % | 11.1 |
| alns | learned z=0.00 +R45 | 5778.81 | 5797.04 | 3.6 | 1.4 | 0.9 % | 1.1 % | 1.6 % | -96 % | 0.2 % | 10.8 |
| alns | learned z=0.00 +R60 | 7192.34 | 7206.79 | 7.0 | 2.2 | 0.5 % | 0.6 % | 0.8 % | -98 % | 0.1 % | 10.1 |
| alns | learned z=0.50 | 5387.33 | 5391.10 | 2.8 | 1.2 | 10.5 % | 5.7 % | 13.6 % | -65 % | 2.5 % | -2.2 |
| alns | learned z=0.50 +R15 | 5775.16 | 5773.13 | 3.8 | 1.4 | 3.2 % | 2.4 % | 4.8 % | -88 % | 0.8 % | -2.6 |
| alns | learned z=0.50 +R30 | 6761.92 | 6761.06 | 6.0 | 2.0 | 1.0 % | 1.0 % | 1.8 % | -95 % | 0.2 % | -3.0 |
| alns | learned z=0.50 +R45 | 8495.40 | 8493.39 | 8.8 | 3.4 | 0.3 % | 0.6 % | 0.8 % | -98 % | 0.1 % | -3.2 |
| alns | learned z=0.50 +R60 | 9460.00 | 9455.13 | 11.4 | 4.0 | 0.1 % | 0.2 % | 0.3 % | -99 % | 0.0 % | -3.6 |

Rules broken in reality (Monte Carlo, duties):

- alns | v0: daily_service_max 2; shift 1; weekly_service_max 153; work_after_break 303; work_before_break 1173; work_without_break 1996;
- alns | learned z=0.00: daily_service_max 1; weekly_service_max 239; work_after_break 87; work_before_break 1205; work_without_break 906;
- alns | learned z=0.00 +R15: daily_service_max 1; weekly_service_max 25; work_after_break 9; work_before_break 660; work_without_break 305;
- alns | learned z=0.00 +R30: daily_service_max 2; shift 1; weekly_service_max 15; work_after_break 19; work_before_break 263; work_without_break 190;
- alns | learned z=0.00 +R45: daily_service_max 1; weekly_service_max 7; work_after_break 5; work_before_break 121; work_without_break 71;
- alns | learned z=0.00 +R60: daily_service_max 1; shift 1; work_after_break 2; work_before_break 80; work_without_break 37;
- alns | learned z=0.50: daily_service_max 1; shift 1; weekly_service_max 17; work_after_break 66; work_before_break 687; work_without_break 271;
- alns | learned z=0.50 +R15: daily_service_max 1; weekly_service_max 14; work_after_break 55; work_before_break 201; work_without_break 177;
- alns | learned z=0.50 +R30: daily_service_max 1; shift 1; weekly_service_max 7; work_after_break 25; work_before_break 96; work_without_break 68;
- alns | learned z=0.50 +R45: daily_service_max 1; shift 1; weekly_service_max 12; work_after_break 32; work_before_break 28; work_without_break 39;
- alns | learned z=0.50 +R60: daily_service_max 2; shift 2; weekly_service_max 4; work_after_break 22; work_before_break 11; work_without_break 9;
