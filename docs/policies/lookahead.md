# Week policies: look-ahead and scarce-skill reserve (V1-T5)

Config `instance_myopia.yaml`. Myopic = rolling MILP per day (60 s, gap 0.1 %). Window MILP 120 s, gap 0.1 %. Clairvoyant 600 s, gap 0.5 %. Reserve cost 5 EUR/min. True weekly costs (EUR), recovery of the myopic - clairvoyant gap in brackets.

| seed | myopic | clairvoyant | LA2 | LA3 | R1 | LA2+R |
|---|---|---|---|---|---|---|
| 1 | 2790.67 | 2633.21 | 2633.73 (100 %) | 2633.73 (100 %) | 2790.67 (0 %) | 2633.73 (100 %) |
| 2 | 4216.55 | 2299.53 | 2419.26 (94 %) | 2419.26 (94 %) | 2492.95 (90 %) | 2419.26 (94 %) |
| 3 | 1947.51 | 1947.46 | 1947.51 (0 %) | 1947.51 (0 %) | 1947.51 (0 %) | 1947.51 (0 %) |
| 4 | 1799.25 | 1799.06 | 1799.11 (74 %) | 1799.11 (74 %) | 1799.25 (0 %) | 1799.11 (74 %) |
| 5 | 2419.34 | 2257.06 | 2312.07 (66 %) | 2298.35 (75 %) | 2419.34 (0 %) | 2312.07 (66 %) |

Aggregate recovery (sum over seeds): LA2 92.2 % (worst vs myopic +0.00 %); LA3 92.8 % (worst vs myopic +0.00 %); R1 77.0 % (worst vs myopic +0.00 %); LA2+R 92.2 % (worst vs myopic +0.00 %);
