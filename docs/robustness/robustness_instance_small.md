# Robustness to true service times (V1-T6), `instance_small.yaml`

Plans made with the estimate 10 + 6 x pallets, executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality.

| solver | planned cost (EUR, mean) | duties | late (truth.json) | illegal (truth.json) | late (MC) | illegal (MC) | late or illegal (MC) | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|
| baseline | 7095.08 | 99 | 22.2 % | 19.2 % | 22.9 % | 14.1 % | 29.2 % | 5.9 % | 23.4 |
| alns | 4527.08 | 91 | 33.0 % | 19.8 % | 30.9 % | 19.6 % | 39.3 % | 7.8 % | 25.6 |
| milp | 4153.65 | 91 | 30.8 % | 23.1 % | 29.5 % | 21.9 % | 40.4 % | 6.8 % | 25.6 |

Rules broken in reality (Monte Carlo, duties): baseline { weekly_service_max: 3 work_after_break: 163 work_before_break: 205 work_without_break: 2413 }; alns { daily_service_max: 2 shift: 1 weekly_service_max: 153 work_after_break: 303 work_before_break: 1173 work_without_break: 1996 }; milp { daily_service_max: 2 shift: 1 weekly_service_max: 117 work_after_break: 190 work_before_break: 1473 work_without_break: 2260 };
