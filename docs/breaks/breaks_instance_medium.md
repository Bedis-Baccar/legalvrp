# Break rules V1-T8 vs V0, `instance_medium`

Seeds 1-5 (5 certified weeks). V0 = one 45-min break; V1 = + a lone 30-min break (work <= 9 h, driving <= 4 h 30) and the 15 + 30 split. ALNS 5000 iterations/day. Means per week.

| solver | rules | cost (EUR) | vs V0 | postponements | unserved | paid hours | duties | with 45 | with 30 | with 15+30 |
|---|---|---|---|---|---|---|---|---|---|---|
| alns | V0 | 7897.91 |  | 3.0 | 1.0 | 153.2 | 25.8 | 16.0 | 0.0 | 0.0 |
| alns | V1 | 7935.78 | 0.48 % | 2.6 | 1.2 | 152.8 | 25.8 | 14.4 | 0.4 | 1.0 |
