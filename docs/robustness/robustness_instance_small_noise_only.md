# Robustness to true service times (V1-T6), `instance_small.yaml`

Plans made with the estimate 10 + 6 x pallets, executed with the true durations (lognormal by customer type, `service_true` in the config). Seeds 1-5; Monte Carlo: 200 scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any other rule breaks in reality. **Noise only**: true mean = the planning estimate, same cv (no estimation bias).

| solver | planned cost (EUR, mean) | duties | late (truth.json) | illegal (truth.json) | late (MC) | illegal (MC) | late or illegal (MC) | late stops (MC) | mean overrun (min) |
|---|---|---|---|---|---|---|---|---|---|
| baseline | 7095.08 | 99 | 6.1 % | 6.1 % | 12.4 % | 6.3 % | 15.6 % | 3.0 % | 9.4 |
| alns | 4527.08 | 91 | 15.4 % | 8.8 % | 17.2 % | 9.0 % | 22.5 % | 4.0 % | 10.2 |
| milp | 4345.49 | 91 | 14.3 % | 11.0 % | 14.6 % | 11.3 % | 21.8 % | 3.3 % | 10.1 |

Rules broken in reality (Monte Carlo, duties): baseline { weekly_service_max: 1 work_after_break: 45 work_before_break: 48 work_without_break: 1161 }; alns { weekly_service_max: 50 work_after_break: 105 work_before_break: 390 work_without_break: 1096 }; milp { weekly_service_max: 101 work_after_break: 11 work_before_break: 837 work_without_break: 1125 };
