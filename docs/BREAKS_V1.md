# Break rules as the law states them (V1-T8) — 2026-10-04

**Question.** V0 allowed one simplification of the break rules: a single 45-min break, or none.
The law also allows a **30-min break** when the day's work is 6–9 h, and a **15 + 30 split**.
Q4 selected these two (the 10-h driving extension was dropped). What do they change?

Specification: [MODEL.md](MODEL.md), amendment V1-T8. Data: [`breaks/`](breaks/).

## 1. The rules

| source | rule |
|---|---|
| Dir. 2002/15/EC art. 5, Code des transports L3312-2 | ≤ 6 h of consecutive work; breaks total ≥ 30 min for 6–9 h of work, ≥ 45 min above 9 h; parts of ≥ 15 min count |
| Reg. 561/2006 art. 7 | after ≤ 4 h 30 of driving, 45 min, or 15 min followed later by 30 min |

**The checker** now applies the law to any list of breaks (a route carries
`breaks: [{after, minutes}]`). Plan files of V0 (`break_after_order_id`) are still read, as one
45-min break. With one 45-min break the checks reduce exactly to V0's, and all V0 labels hold.
19 labelled duties were added to the corpus, now 60. They cover:

- the 9-h boundary (540 / 541 min);
- a lone 30 min that does not restart driving;
- "30 then 15", which is not a split;
- work between two parts;
- 45 + 45 with too much driving between them;
- parts under 15 min, and breaks out of order.

**The planners** (exact evaluator, fast evaluator, ALNS, enumeration, both MILP formulations)
choose per route among four patterns: none, 45, a lone 30 (work ≤ 9 h, driving ≤ 4 h 30), or
15 then 30. `allow_short_break` and `allow_split_break` in `config/rules.yaml` switch the new
ones off, which reproduces V0.

## 2. Verification

| check | result |
|---|---|
| checker corpus (60 duties, 19 for V1-T8) | every label reproduced |
| fast evaluator = exact STN evaluator, 100 000 random routes and moves | **0 mismatches**; the new patterns are chosen on 1 013 (30 min) and 688 (split) routes; still 57× faster (0.095 µs vs 5.4 µs) |
| exact evaluator vs integer brute force (400 routes × 3 rule sets) | equal with V0 and with the 30-min break; with the split, never worse, and every route accepted by the checker |
| MILP (both formulations) = enumeration | 20 regime days with the V1 rules, 5 with V0 rules, tiny, both traps, and two hand cases where only the new patterns serve every order (the MILP finds `A:15 B:30` and the lone 30) |
| a plan made under V1 rules is always legal | checker in every rolling week (`legalvrp-breaks`), zero violations |

## 3. What they save

**Exact, day by day.** On the 20 regime days (enumeration, the same days under both rule sets)
the V1 optimum is never above V0. It is lower on **4 / 20 days, by 8.4 % on those days**: tight
duties where the 30-min break or the split lets one more order be served.

**Rolling weeks** (`legalvrp-breaks`, seeds 1–5, true weekly costs; ALNS deterministic):

| instance | solver | rules | cost (€) | vs V0 | postponements | unserved | duties with 45 / 30 / 15+30 |
|---|---|---|---|---|---|---|---|
| small | ALNS | V0 | 4 527.08 | — | 2.0 | 0.4 | 8.6 / 0 / 0 |
| small | ALNS | V1 | 4 311.55 | −4.8 % | 1.4 | 0.2 | 7.8 / 0.4 / 0.8 |
| small | MILP 30 s/day | V0 | 4 351.63 | — | 1.6 | 0.2 | 9.0 / 0 / 0 |
| small | MILP 30 s/day | V1 | 4 317.65 | −0.8 % | 1.6 | 0.2 | 4.0 / 2.8 / 3.0 |
| medium | ALNS | V0 | 7 897.91 | — | 3.0 | 1.0 | 16.0 / 0 / 0 |
| medium | ALNS | V1 | 7 935.78 | +0.5 % | 2.6 | 1.2 | 14.4 / 0.4 / 1.0 |
| large n = 60 | ALNS | V0 | 11 409.88 | — | 1.2 | 0.0 | 28.8 / 0 / 0 |
| large n = 60 | ALNS | V1 | 11 323.82 | −0.75 % | 0.8 | 0.0 | 28.2 / 2.8 / 1.0 |

- **The MILP uses the new patterns most**: a 30-min break or a split on 32 % of the `small`
  duties. It places the 15-min part in waiting time, which ALNS's insertion moves rarely
  discover. Its saving is about 1 %, and it is never worse than V0 except on one week at the
  30-s time limit.
- ALNS uses them in 5–8 % of the duties. On these instances most duties stay well below the
  limits where the patterns matter: they end before the shift and before 9 h of work.
- Weekly differences are dominated by the rolling loop's own noise. On `small` seed 4, the V0
  ALNS week drifts into 3 postponements and an unserved order, while the V1 week does not
  (−22 %). On `medium` seed 3, the opposite happens. Both are differences of trajectory, not
  of a worse day: day by day, V1 is never worse when solved exactly.
- **Default: both switches on** (D-117). They are legal, cost nothing when unused, and help on
  tight days.

## 4. Limits

- Only the two relaxations of Q4. The 10-h driving extension, reduced daily rests and
  availability time ("temps à disposition" counted apart from work) are not modelled; waiting
  is still counted as work.
- The planners' split is exactly 15 then 30 at two different stops. The checker accepts any legal
  list (e.g. 15 + 15 for 6–9 h of work).
