# Price of myopia (task T11) — 2026-10-03

**Question.** The rolling loop plans each day knowing only that day's orders. How much does that
cost against a planner that sees the whole week (clairvoyant)?

**Method.** `legalvrp-myopia --config config/instance_myopia.yaml` (5 days, 4–6 orders/day, one
part-time driver — 06:00, the only tail-lift truck, 24 h threshold, 26.4 h weekly cap — and one
full-time driver, 07:00). For seeds 1–5: certified week → rolling strong MILP per day (60 s,
MIPGap 0.1 %) → clairvoyant weekly MILP (`model/clairvoyant`, D-044; 600 s, MIPGap 0.5 %;
rolling plans as start) → both weeks replayed through the rolling machinery and checked in week
mode. Costs are **true weekly costs** (D-042). Raw data: [`myopia/myopia.csv`](myopia/myopia.csv).
**Both weeks pass the week-mode checker for every seed.**

| seed | orders | rolling (€) | clairvoyant (€) | clairvoyant status | price of myopia |
|---|---|---|---|---|---|
| 1 | 24 | 2 790.67 | 2 633.21 | optimal, 0.0 % | 157.47 € (5.6 %) |
| 2 | 28 | 4 216.55 | 2 299.53 | optimal, 0.3 % | **1 917.02 € (45.5 %)** |
| 3 | 25 | 1 947.51 | 1 947.46 | optimal, 0.0 % | 0.0 % |
| 4 | 25 | 1 799.25 | 1 799.06 | optimal, 0.4 % | 0.0 % |
| 5 | 27 | 2 419.34 | 2 257.06 | optimal, 0.0 % | 162.28 € (6.7 %) |

Median 5.6 %, mean 11.6 %; clairvoyant solved in 9–177 s.

## What causes it

- **Seed 2 (45 %) — a scarce skill and a weekly cap.** The part-time driver drives the only
  tail-lift truck. Day by day, the rolling MILP also gives him ordinary early-morning work (his
  06:00 start suits the grocery windows), so by Friday he is at 24.7 h of his 26.4 h cap. Two
  tail-lift grocery orders (Thursday, Friday) then cannot be served by anyone and stay unserved
  at ≥ 1 000 € each. The clairvoyant plan keeps his hours for the work only he can do and gives
  the rest to the full-time driver, who works 15 h in the rolling week.
- **Seeds 1, 5 (6–7 %) — overtime and postponements.** Seed 5: 142 overtime minutes rolling vs
  31 clairvoyant; the weekly model spreads the part-timer's hours.
- **Seeds 3, 4 (0 %)** — no binding weekly resource: myopia is free.

## Consequences

1. The rolling MILP is near-optimal most weeks, but **weekly resources that are both scarce and
   capped (special equipment × weekly hours) can make myopia very expensive**. The `small`
   week of T8 showed the mild form (part-time overtime, Gini 0.10 vs 0.05).
2. Cheap remedies, without a full weekly model, for V1 (not implemented):
   reserve capacity of scarce-skill drivers for the orders only they can serve (a look-ahead
   term or a soft budget per day on capped drivers), or plan with a short horizon of known
   orders (e.g. today + tomorrow).
3. The clairvoyant model is a valid **bound** for evaluating such policies on reduced instances;
   it does not scale (orders accumulate across the week's daily models).
