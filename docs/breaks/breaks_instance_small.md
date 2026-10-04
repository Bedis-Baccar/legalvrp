# Break rules V1-T8 vs V0, `instance_small`

Seeds 1-5 (5 certified weeks). V0 = one 45-min break; V1 = + a lone 30-min break (work <= 9 h, driving <= 4 h 30) and the 15 + 30 split. ALNS 5000 iterations/day, MILP 30 s/day. Means per week.

| solver | rules | cost (EUR) | vs V0 | postponements | unserved | paid hours | duties | with 45 | with 30 | with 15+30 |
|---|---|---|---|---|---|---|---|---|---|---|
| alns | V0 | 4527.08 |  | 2.0 | 0.4 | 97.7 | 18.2 | 8.6 | 0.0 | 0.0 |
| alns | V1 | 4311.55 | -4.76 % | 1.4 | 0.2 | 98.7 | 18.4 | 7.8 | 0.4 | 0.8 |
| milp | V0 | 4351.63 |  | 1.6 | 0.2 | 98.7 | 18.2 | 9.0 | 0.0 | 0.0 |
| milp | V1 | 4317.65 | -0.78 % | 1.6 | 0.2 | 98.5 | 18.2 | 4.0 | 2.8 | 3.0 |
