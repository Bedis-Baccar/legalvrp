# Robustness to true service times, `instance_medium.yaml`

Plans executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = learned estimator, mu + Z sigma per stop; `+RN` = also a pooled time reserve of N minutes before every limit and window end (the plan is judged against the real limits). Planned cost = the plan's weekly cost; realised cost = the same plans executed with `truth.json` (true weekly cost, checker in week mode).

| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| alns | v0 | 7897.91 | 7979.72 | 3.0 | 1.0 | 40.5 % | 26.0 % | 49.1 % |  | 9.8 % | 29.7 |
| alns | learned z=0.00 | 8284.09 | 8316.11 | 3.6 | 1.2 | 29.2 % | 15.1 % | 35.3 % | -28 % | 6.8 % | 14.5 |
| alns | learned z=0.00 +R15 | 8642.15 | 8663.65 | 3.8 | 1.6 | 12.4 % | 7.7 % | 16.8 % | -66 % | 2.9 % | 14.1 |
| alns | learned z=0.00 +R30 | 9488.87 | 9531.12 | 5.8 | 2.2 | 4.6 % | 3.0 % | 6.4 % | -87 % | 1.0 % | 13.9 |
| alns | learned z=0.00 +R45 | 10572.00 | 10617.81 | 7.8 | 2.8 | 1.9 % | 1.6 % | 2.9 % | -94 % | 0.4 % | 13.5 |
| alns | learned z=0.00 +R60 | 11773.20 | 11809.38 | 9.2 | 3.6 | 0.6 % | 0.8 % | 1.2 % | -98 % | 0.1 % | 12.9 |
| alns | learned z=0.50 | 8679.83 | 8673.17 | 4.0 | 1.2 | 15.7 % | 8.1 % | 20.3 % | -59 % | 3.4 % | -0.2 |
| alns | learned z=0.50 +R15 | 9053.92 | 9053.82 | 3.8 | 1.6 | 5.1 % | 2.5 % | 6.7 % | -86 % | 1.1 % | -1.1 |
| alns | learned z=0.50 +R30 | 11326.66 | 11340.33 | 8.8 | 3.2 | 1.6 % | 1.1 % | 2.2 % | -96 % | 0.3 % | -1.4 |
| alns | learned z=0.50 +R45 | 11442.29 | 11459.46 | 8.4 | 3.2 | 0.8 % | 0.7 % | 1.2 % | -98 % | 0.2 % | -1.2 |
| alns | learned z=0.50 +R60 | 14091.14 | 14091.29 | 13.4 | 5.2 | 0.3 % | 0.4 % | 0.6 % | -99 % | 0.1 % | -1.4 |

Rules broken in reality (Monte Carlo, duties):

- alns | v0: daily_service_max 2; shift 8; weekly_service_max 233; work_after_break 449; work_before_break 2457; work_without_break 3662;
- alns | learned z=0.00: daily_service_max 1; shift 1; weekly_service_max 104; work_after_break 301; work_before_break 2102; work_without_break 1384;
- alns | learned z=0.00 +R15: daily_service_max 4; shift 8; weekly_service_max 100; work_after_break 79; work_before_break 1013; work_without_break 856;
- alns | learned z=0.00 +R30: daily_service_max 1; shift 4; weekly_service_max 62; work_after_break 126; work_before_break 407; work_without_break 221;
- alns | learned z=0.00 +R45: daily_service_max 1; shift 1; weekly_service_max 2; work_after_break 86; work_before_break 235; work_without_break 99;
- alns | learned z=0.00 +R60: daily_service_max 1; shift 3; weekly_service_max 3; work_after_break 55; work_before_break 138; work_without_break 32;
- alns | learned z=0.50: daily_service_max 4; shift 6; weekly_service_max 199; work_after_break 258; work_before_break 1185; work_without_break 515;
- alns | learned z=0.50 +R15: daily_service_max 2; shift 7; weekly_service_max 40; work_after_break 151; work_before_break 425; work_without_break 64;
- alns | learned z=0.50 +R30: daily_service_max 2; shift 3; weekly_service_max 1; work_after_break 46; work_before_break 172; work_without_break 68;
- alns | learned z=0.50 +R45: daily_service_max 1; shift 3; weekly_service_max 1; work_after_break 81; work_before_break 106; work_without_break 7;
- alns | learned z=0.50 +R60: daily_service_max 2; shift 3; weekly_service_max 2; work_after_break 55; work_before_break 60; work_without_break 2;
