# Robustness to true service times (V1-T6), `instance_medium.yaml`

Plans made with the estimate 10 + 6 x pallets, executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality.

| solver | planned cost (EUR, mean) | duties | late (truth.json) | illegal (truth.json) | late (MC) | illegal (MC) | late or illegal (MC) | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|
| baseline | 12574.84 | 148 | 30.4 % | 21.6 % | 28.2 % | 19.4 % | 38.1 % | 8.0 % | 25.9 |
| alns | 7897.91 | 129 | 41.1 % | 25.6 % | 40.5 % | 26.0 % | 49.1 % | 9.8 % | 29.7 |
| milp | 9505.61 | 134 | 31.3 % | 23.9 % | 34.4 % | 24.6 % | 43.8 % | 8.8 % | 28.5 |

Rules broken in reality (Monte Carlo, duties): baseline { daily_service_max: 1 shift: 2 weekly_service_max: 312 work_after_break: 250 work_before_break: 1224 work_without_break: 3974 }; alns { daily_service_max: 2 shift: 8 weekly_service_max: 233 work_after_break: 449 work_before_break: 2457 work_without_break: 3662 }; milp { daily_service_max: 4 shift: 5 weekly_service_max: 149 work_after_break: 363 work_before_break: 2638 work_without_break: 3567 };
