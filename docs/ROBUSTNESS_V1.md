# Robustness to uncertain service times (V1-T6) — 2026-10-04

**Question.** Every plan so far assumes the service time at a stop is exactly 10 + 6 × pallets
minutes. What happens to those plans when the stops take their *true* time?

Raw data: [`robustness/`](robustness/) (CSV + generated tables).

## 1. Truth model (generator side, D-109)

| customer type | planned (3 pallets) | true mean | cv | true mean, 3 pallets |
|---|---|---|---|---|
| grocery | 10 + 6p = 28 min | 12 + 6p | 0.30 | 30 min (+7 %) |
| restaurant | 10 + 6p | 14 + 7p | 0.45 | 35 min (+25 %; +29 % at 1.5 pallets) |
| site | 10 + 6p | 18 + 6p | 0.55 | 36 min (+29 %) |

True minutes are lognormal (skewed, never negative), drawn from their own RNG stream. They go
into `truth.json` (realised minutes, true mean and σ per order); `week.json` and the planners
are unchanged. These values are a **synthetic assumption**: the planning rule is close for
supermarkets and optimistic for town-centre restaurants and building sites. It is
reproducible per seed and identical on MSVC and GCC (golden fixtures).

## 2. How a plan is executed (D-110)

The driver leaves at the planned time, keeps the planned sequence and break position, and
starts each service at max(arrival, planned start). An early arrival waits for the
appointment; a delay eats into the planned waiting slack, and the rest propagates. Nothing is
re-planned during the day. The realised day goes to the **same independent checker**:

- **late** — a service starts after the customer's window;
- **illegal** — any other rule breaks in reality. In practice this is almost always a work
  block > 6 h without a break: the driver would have to take an unplanned break and finish
  later. Rarer causes are the weekly service cap, the end of shift and the daily service maximum.

`legalvrp-robustness` plans rolling weeks with each solver (baseline = current practice;
ALNS 5 000 iterations/day; strong MILP, 30 s/day on `small`, 60 s on `medium`), then executes
every duty (a) with the week's `truth.json` and (b) in 200 Monte Carlo scenarios per week
(the same scenarios for every solver). Seeds 1–5.

## 3. Results

Share of duties (Monte Carlo, 200 scenarios × 5 weeks):

| instance | solver | planned cost (€) | late | illegal | **late or illegal** | late stops | mean overrun |
|---|---|---|---|---|---|---|---|
| small | baseline | 7 095 | 22.9 % | 14.1 % | **29.2 %** | 5.9 % | 23 min |
| small | ALNS | 4 527 | 30.9 % | 19.6 % | **39.3 %** | 7.8 % | 26 min |
| small | MILP (V0) | 4 154 | 29.5 % | 21.9 % | **40.4 %** | 6.8 % | 26 min |
| medium | baseline | 12 575 | 28.2 % | 19.4 % | **38.1 %** | 8.0 % | 26 min |
| medium | ALNS | 7 898 | 40.5 % | 26.0 % | **49.1 %** | 9.8 % | 30 min |
| medium | MILP (V0) | 9 506 | 34.4 % | 24.6 % | **43.8 %** | 8.8 % | 29 min |

On the single realisation in `truth.json` the shares are the same within noise
(`small`, MILP: 28/91 duties late, 21/91 illegal; ALNS: 30/91 and 18/91).

**Bias or noise?** Run again with *unbiased* truths (`--noise-only`: true mean = the planning
estimate, same cv), on `small`:

| solver | late or illegal, biased truth | late or illegal, noise only | mean overrun |
|---|---|---|---|
| baseline | 29.2 % | 15.6 % | 23 → 9 min |
| ALNS | 39.3 % | 22.5 % | 26 → 10 min |
| MILP | 40.4 % | 21.8 % | 26 → 10 min |

## 4. What it means

1. **Deterministic plans are fragile.** With realistic variability, 40–50 % of the optimised
   duties cannot be executed as planned. 7–10 % of their stops are served late, and one duty in
   four or five breaks a working-time rule as executed.
2. **The better the plan, the more fragile it is.** ALNS and the MILP pack duties against the 6-h
   work limit and the windows. The current-practice baseline wastes time, and that waste acts
   as a buffer (29 % vs 40 % on `small`). Cost and robustness have to be planned together.
3. **About half of the damage is estimation bias, half is variance.** A better *mean* (learned
   from history by customer type × pallets) would remove roughly half of the failures. The
   other half needs buffers (planning with a quantile, or μ + zσ per stop). This is exactly the
   design of V1-T7, and these tables are its baseline: T7 must cut late/illegal duties by
   ≥ 80 % from these figures, at a reported cost.
4. The MILP with a time limit is not run-to-run deterministic. Its planned weeks differ
   slightly between the two `small` runs (4 154 € vs 4 345 €), which does not change the
   conclusions.

## 5. Limits

- The truth model is synthetic (no real stop-time data). The conclusions depend on its
  bias and cv, which is why the noise-only run is reported separately.
- Delays are independent between stops (no day-level shocks such as traffic). Correlated
  delays would make things worse.
- Weekly caps are judged against the planned weekly state; realised hours are not carried to
  the next day's planning.
