# Week policies (V1-T5) — 2026-10-04

**Question.** The rolling loop plans each day on its own. V0 measured what that costs against a
planner that sees the whole week ([MYOPIA_V0.md](MYOPIA_V0.md): +5.6 % median, +45 % on one seed).
Which cheap policies recover that loss, and what does a fairness goal (Q1) cost?

Raw data: [`policies/`](policies/) (CSV + generated tables). Every week below passes the
week-mode checker.

## 1. Policies

| policy | what the day solver sees | implementation |
|---|---|---|
| myopic (V0) | today's orders + carried orders, weekly state | `week::solve_day` (strong MILP) |
| **LA2** — look-ahead | today **and tomorrow** (Q2: tomorrow's orders are known); commits today only | `model::solve_window` on [d, d+1], `week::solve_day_lookahead` |
| LA3 | today + the next two days | same, window of 3 days |
| R1 — scarce-skill reserve | today only, but a driver who alone can serve some orders (tail-lift, pallets) keeps capacity for the days after the window: soft budget, 5 €/min of reserve used | `WindowSpec::reserve`, `week::scarce_reserve` |
| LA2+R | both | |

**Window model.** The clairvoyant weekly model of T11 was generalised to any window
[first, last] of the week: it starts from the real weekly state (W₀, V₀ of every driver)
and the carried orders, pays the rolling loop's penalties on the real days (escalation;
end-of-week penalty only on the week's last day), and puts the weekly caps and overtime on
state + window. `solve_week_clairvoyant` is the window [0, D−1] from a zero state. Checks:
the T11 trap is unchanged (869.40 vs 595.20). On every day of a generated week, a one-day
window started from the rolling state has **exactly the daily MILP's optimum**
(`test_clairvoyant [window]`).

**Look-ahead day.** The day is first solved myopically (MIP start and fallback). The
following days are then filled with the baseline, built on that plan's carry-over and state.
Next the window MILP runs. Its day-d plan goes through the checker (and the exact re-timing
if needed). If anything fails, the myopic plan is committed. On the week's last day there is
nothing to look ahead to, so the myopic plan is used.

## 2. Results on the V0 myopia instances (seeds 1–5)

`legalvrp-policies lookahead --config config/instance_myopia.yaml`. Myopic MILP 60 s/day,
window MILP 120 s, clairvoyant 600 s; true weekly costs (€), share of the myopic → clairvoyant
gap recovered in brackets.

| seed | myopic | clairvoyant | **LA2** | LA3 | R1 | LA2+R |
|---|---|---|---|---|---|---|
| 1 | 2 790.67 | 2 633.21 | **2 633.73 (100 %)** | 2 633.73 (100 %) | 2 790.67 (0 %) | 2 633.73 (100 %) |
| 2 | 4 216.55 | 2 299.53 | **2 419.26 (94 %)** | 2 419.26 (94 %) | 2 492.95 (90 %) | 2 419.26 (94 %) |
| 3 | 1 947.51 | 1 947.46 | 1 947.51 (gap 0.05 €) | 1 947.51 | 1 947.51 | 1 947.51 |
| 4 | 1 799.25 | 1 799.06 | 1 799.11 (gap 0.19 €) | 1 799.11 | 1 799.25 | 1 799.11 |
| 5 | 2 419.34 | 2 257.06 | **2 312.07 (66 %)** | 2 298.35 (75 %) | 2 419.34 (0 %) | 2 312.07 (66 %) |
| **recovered (Σ over seeds)** | | | **92.2 %** | 92.8 % | 77.0 % | 92.2 % |
| worst vs myopic | | | +0.00 % | +0.00 % | +0.00 % | +0.00 % |

- **Acceptance met by LA2**: 92 % of the gap recovered (target ≥ 70 %). Over the three seeds
  with a real gap, the mean is 87 %. It is never worse than myopic (target ≤ +1 %). The
  windows add **1.3–2.7 s of solver time per week**, against 9–152 s for the clairvoyant
  model.
- **Seed 2** (the scarce tail-lift driver whose weekly cap ran out): seeing tomorrow is
  enough to stop giving the part-timer ordinary work. 2 unserved orders → 0, and the cost goes
  from +83 % above clairvoyant to +5 %.
- **LA3** adds almost nothing (seed 5 only) for 3–8× the window time: tomorrow carries most of
  the value.
- **The reserve** alone (R1) fixes seed 2 (90 %) and nothing else, which is what it is
  designed for. On top of LA2 it changes nothing. It is kept **off by default** and is
  useful only when tomorrow's orders are *not* known.

## 3. Fairness (Q1: balanced workload is a goal)

ALNS gets a fairness term: *w* € per minute of (max − min) of the full-time drivers' projected
weekly service (weekly state + today). It is exact in insertion, removal, polish and
acceptance (O(1) per move from the two extremes). With *w* = 0 the search is unchanged.
`legalvrp-policies fairness`, rolling ALNS week (5 000 iterations/day, deterministic),
seeds 1–5.

**small** (3 full-time drivers), seeds 1–3. Seeds 4–5 are excluded from the cost column for
one reason: at *w* = 0 the week ends with an unserved order (≥ 1 000 €), and any *w* > 0
changes the week's trajectory and avoids it. That is a side effect of myopia, not a fairness
effect. All 5 seeds are in [`policies/fairness_instance_small.md`](policies/fairness_instance_small.md).

| *w* (€/min) | cost (€) | vs *w* = 0 | Gini (full-time hours) | spread max − min (h) |
|---|---|---|---|---|
| 0 | 3 795.29 | — | 0.142 | 15.1 |
| 0.05 | 3 803.58 | +0.2 % | 0.083 | 8.8 |
| 0.1 | 3 807.00 | +0.3 % | 0.041 | 4.7 |
| **0.2** | **3 796.32** | **+0.03 %** | **0.040** | **4.7** |
| 0.5 | 3 894.90 | +2.6 % | 0.010 | 1.4 |
| 1 | 3 926.06 | +3.4 % | 0.008 | 1.1 |

**medium** (4 full-time drivers), means over seeds 1–5: Gini 0.032 → 0.024 (*w* 0.1) →
0.013 (0.2) → 0.007 (0.5) → 0.003 (1). The weekly costs move by up to ±15 % per seed, because
0–2 orders stay unserved at the end of the week and that count changes with any change of
trajectory. The cost shows no systematic sign: −0.4 % on average at *w* = 0.2
([`policies/fairness_instance_medium.md`](policies/fairness_instance_medium.md)).

**Default *w* = 0.2 €/min** for `legalvrp-run-week --solver alns`. It sits at the knee of the
curve: it removes about 70 % of the hours spread for no measurable cost. For scale, it is about
half the overtime rate (0.45 €/min).

## 4. Limits

- The look-ahead and the clairvoyant bound run on the reduced myopia instances (2 drivers,
  4–6 orders/day), because that is where the clairvoyant bound exists. On larger weeks the
  window MILP grows like two daily models. An ALNS version of the window (two days in one
  solution, commit day 1) is the natural extension and is not done.
- Five seeds; the recovery on seed 5 alone (66 %) is below 70 %.
- Fairness is on full-time drivers only (part-time and temp contracts are paid per hour and
  are not "balanced").
