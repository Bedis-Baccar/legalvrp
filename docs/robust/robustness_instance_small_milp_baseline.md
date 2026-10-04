# Robustness to true service times, `instance_small.yaml`

Plans executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = learned estimator, mu + Z sigma per stop; `+RN` = also a pooled time reserve of N minutes before every limit and window end (the plan is judged against the real limits). Planned cost = the plan's weekly cost; realised cost = the same plans executed with `truth.json` (true weekly cost, checker in week mode).

| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| milp | v0 | 4343.89 | 4382.29 | 1.6 | 0.2 | 28.7 % | 22.3 % | 38.5 % |  | 6.9 % | 25.6 |
| milp | learned z=0.50 | 5768.22 | 5761.02 | 4.4 | 1.2 | 9.9 % | 6.5 % | 14.3 % | -63 % | 2.3 % | -2.0 |
| milp | learned z=0.50 +R15 | 6628.15 | 6633.17 | 5.6 | 2.0 | 3.7 % | 2.8 % | 5.6 % | -86 % | 0.8 % | -2.6 |
| baseline | v0 | 7095.08 | 7120.70 | 8.2 | 1.6 | 22.9 % | 14.1 % | 29.2 % |  | 5.9 % | 23.4 |
| baseline | learned z=0.50 | 9975.77 | 9970.30 | 14.0 | 3.8 | 9.1 % | 5.4 % | 12.4 % | -57 % | 2.3 % | -1.9 |
| baseline | learned z=0.50 +R15 | 11622.86 | 11613.54 | 16.2 | 5.2 | 3.3 % | 2.4 % | 5.0 % | -83 % | 0.9 % | -1.6 |

Rules broken in reality (Monte Carlo, duties):

- milp | v0: daily_service_max 1; weekly_service_max 184; work_after_break 56; work_before_break 2019; work_without_break 1850;
- milp | learned z=0.50: daily_service_max 1; shift 1; weekly_service_max 85; work_after_break 11; work_before_break 701; work_without_break 389;
- milp | learned z=0.50 +R15: daily_service_max 1; shift 2; weekly_service_max 18; work_after_break 103; work_before_break 261; work_without_break 138;
- baseline | v0: weekly_service_max 3; work_after_break 163; work_before_break 205; work_without_break 2413;
- baseline | learned z=0.50: daily_service_max 1; shift 1; weekly_service_max 19; work_after_break 81; work_before_break 261; work_without_break 690;
- baseline | learned z=0.50 +R15: daily_service_max 1; shift 2; weekly_service_max 12; work_after_break 120; work_before_break 96; work_without_break 244;
