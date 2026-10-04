# Robustness to true service times, `instance_medium.yaml`

Plans executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = learned estimator, mu + Z sigma per stop. Planned cost = the plan's weekly cost; realised cost = the same plans executed with `truth.json` (true weekly cost, checker in week mode).

| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| alns | v0 | 7897.91 | 7979.72 | 3.0 | 1.0 | 40.5 % | 26.0 % | 49.1 % |  | 9.8 % | 29.7 |
| alns | learned z=0.00 | 8284.09 | 8316.11 | 3.6 | 1.2 | 29.2 % | 15.1 % | 35.3 % | -28 % | 6.8 % | 14.5 |
| alns | learned z=0.50 | 8679.83 | 8673.17 | 4.0 | 1.2 | 15.7 % | 8.1 % | 20.3 % | -59 % | 3.4 % | -0.2 |
| alns | learned z=1.00 | 10218.19 | 10172.36 | 7.2 | 2.2 | 8.0 % | 3.7 % | 10.6 % | -78 % | 1.7 % | -12.6 |
| alns | learned z=1.50 | 12312.85 | 12244.24 | 9.6 | 4.0 | 3.6 % | 2.7 % | 5.9 % | -88 % | 0.8 % | -22.5 |
| alns | learned z=2.00 | 15059.32 | 14878.51 | 15.8 | 5.6 | 2.0 % | 1.8 % | 3.6 % | -93 % | 0.4 % | -30.9 |

Rules broken in reality (Monte Carlo, duties):

- alns | v0: daily_service_max 2; shift 8; weekly_service_max 233; work_after_break 449; work_before_break 2457; work_without_break 3662;
- alns | learned z=0.00: daily_service_max 1; shift 1; weekly_service_max 104; work_after_break 301; work_before_break 2102; work_without_break 1384;
- alns | learned z=0.50: daily_service_max 4; shift 6; weekly_service_max 199; work_after_break 258; work_before_break 1185; work_without_break 515;
- alns | learned z=1.00: daily_service_max 3; shift 7; weekly_service_max 49; work_after_break 291; work_before_break 487; work_without_break 166;
- alns | learned z=1.50: daily_service_max 3; shift 6; weekly_service_max 3; work_after_break 257; work_before_break 426; work_without_break 49;
- alns | learned z=2.00: daily_service_max 3; shift 9; weekly_service_max 1; work_after_break 290; work_before_break 169; work_without_break 25;
