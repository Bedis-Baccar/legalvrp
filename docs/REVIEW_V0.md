# Expert review of the V0 specification — 2026-10-02

Scope: `PROJECT_BRIEF.md`, `docs/SIZING.md`, `docs/validation/model_validation.tex`.
Method: each constraint C1–C18 and each big-M was re-derived by hand; the
specification of the evaluator, checker and brute force (§7, §8, T6) was tested
against the MILP's feasible set; the sizing table was recomputed.

## Verdict

**The daily MILP (§6) is correct.** Every constraint does what its rationale says,
every big-M switches its constraint off and is valid, subtours are excluded by C6–C7
because `s_i ≥ 1`, the two-sided C8–C9 and the `DD + τ_ij` lower big-M are right, and
the empty route gives `θ_k = 0` (C16). The sizing table checks out
(n = 8, K = 2: 2·(8·7 + 2·8 + 1) arcs + 16 `y` + 8 `u` = 170 binaries).
The "every day is feasible" property holds provided `P + R ≤ WB` and `τ_0E = 0`.

**No change to the MILP is needed.** Two points elsewhere in the spec must be fixed
before T3/T4/T6. Otherwise the brute-force comparison (T6) can report false
mismatches, and someone could "fix" a correct model to agree with a wrong checker
(§13: "the checker wins").

| # | Severity | Where | Finding | Status |
|---|---|---|---|---|
| F1 | **must fix** | §7 evaluator, §8 checker, T6 brute force | The MILP may put waiting *before* the break (any `T_b ∈ [arrival, l_b]` at the break node). An evaluator that schedules earliest-start after a chosen departure cannot do this, so it is strictly more restrictive than the MILP | proposed D-007 |
| F2 | **must fix** | §4 compatibility | "truck size ≥ customer access class" is reversed: `access_class` is the *largest truck allowed* (§4 Customer, §5.1). Correct rule: `size(truck) ≤ access_class(customer)` | **resolved**: access classes removed (D-008) |
| F3 | should fix | T6 trap (a) | Only the legs of the sequence A→B→C are given. The other legs (A→C, B→A, …) must be specified so that *every* sequence of the three is illegal; otherwise the "must postpone" claim does not follow | fix when writing the fixture |
| F4 | improvement | §6.1 pruning | Valid extra pruning under A7: drop any arc with `τ_ij > DB` (a leg longer than 4 h 30 can never be legal, since breaks happen only at customers); drop `(0,j)` for k if `S_k + P + τ_0j > l_j`; drop `(j,E)` for k if `e_j + s_j + τ_jE + R > F_k`; drop `(i,j)` for k if `q_i + q_j > Q_k` | adopt in T6 |
| F5 | improvement | §6.5 C11 | Tighter, still valid: `M^a_ik = max(F_k − e_i − s_i, l_i + s_i − S_k)` | adopt in T6 |
| F6 | clarity | §6.6 | Give Gurobi a *complete* MIP start (also `T, D, a, b, tE, svc, ext`), all computable from the baseline route. A partial start makes Gurobi solve a sub-MIP to complete it | adopt in T7 |
| F7 | clarity | §6.1 | State explicitly `τ_0E = d_0E = 0` (empty-route arc) | done in MODEL.md |
| F8 | clarity | validation.tex §3 | θ is measured from `t0 − P`, not from `S_k`; they coincide only when departure is not delayed. The model (C16) is right; the figure caption is imprecise | noted |
| F9 | limitation | legal horizon | Not representable in a one-week horizon and therefore out of V0: fortnightly driving ≤ 90 h (561/2006 art. 6.3), averaged weekly working-time limits (2002/15/EC art. 4), the twice-weekly 10 h extension. The state must carry previous-week driving once horizons chain | documented |

## F1 in detail — counter-example

Data: `S = 360, F = 1125, P = 20, R = 10, WB = 360, BR = 45`.
Route 0 → A → B → C → E with B the break node.

| node | window | s | leg in |
|---|---|---|---|
| A | [400, 400] | 20 | τ_0A = 20 |
| B | [440, 480] | 10 | τ_AB = 20 |
| C | [800, 900] | 30 | τ_BC = 30 |
| E | — | — | τ_CE = 40 |

Departure is forced: `t0 = 380` (A is fixed at 400).

* **Earliest-start schedule** (evaluator as specified): T_A = 400, T_B = 440, break
  450–495, arrive C 525, wait to 800, back 870, close until 880.
  Work after break = 880 − 495 = **385 > 360**. Break after A: 880 − 465 = 415.
  Break after C: before-break work 830 − 360 = 470. No break: 520. **Rejected.**
* **MILP schedule:** T_B = 480 (40 min waiting *before* service at B, counted as
  work), break 490–535, work before = 490 − 360 = 130, work after = 880 − 535 =
  **345 ≤ 360**, driving 110. **Legal** under every rule and every assumption A1–A16.

So the MILP serves the route and the brute force postpones an order: a T6
"mismatch" with the MILP right. A checker that recomputes earliest-start times would
also flag the MILP plan, and §13 would then tell the developer to weaken a correct
model.

**Proposed fix (exact, O(n) per break position and departure):** for a fixed
sequence, break node b and departure t0:
1. schedule earliest-start up to b → earliest `T_b`;
2. let `T_b` range over `[earliest T_b, l_b]`, with `a = T_b + s_b`, and schedule
   earliest-start after the break.
   Work after the break, `tE(a) + R − a − BR`, does not increase as `a` grows,
   because `tE(a)` grows by at most as much as `a`. Work before the break and θ do
   not decrease. So the best `T_b` is the **smallest** one that brings work after
   the break down to ≤ WB; then check work before the break.
3. Waiting at nodes before b is irrelevant: only `a` enters the legal quantities.

Checker (§8): verify the **reported** `service_starts` (arrival ≤ T_i,
e_i ≤ T_i ≤ l_i, consistency with travel and service), take the break start as
`T_b + s_b`, and compute every segment from those reported times. Do not
re-schedule. The plan is what the driver will do.

Integrality: for fixed routes, the timing constraints are difference constraints,
so they are totally unimodular. With integer data an integer optimal schedule
exists, so scanning integer minutes (brute force) is exact.

## F2 in detail

`Customer.access_class` = largest truck allowed (§4). §5.1: "one large truck
excluded from S-access customers". Hence `compatible(k, i) ⇔ size(k) ≤ access(i) ∧
(¬needs_tail_lift(i) ∨ tail_lift(k)) ∧ q_i ≤ Q_k`, with S < M < L. The §4 sentence
reads the other way round.

**Resolved 2026-10-02 by D-008:** the owner removed access classes entirely.
Any truck serves any customer, so the question no longer arises.
