# MODEL.md — the daily MILP (single source of truth)

Copied verbatim from `PROJECT_BRIEF.md` §6 on 2026-10-02. From now on **this file wins**:
change it first, then the code (`src/legalvrp/model/`), then log in `docs/DECISIONS.md`.
Code names (`x`, `u`, `T`, `D`, `y`, `t0`, `tE`, `a`, `b`, `svc`, `ext`; `cover`, `leave`, …)
are the Gurobi variable and constraint names.

**Notes added after review (`docs/REVIEW_V0.md`)**
- `τ_0E = d_0E = 0`: the arc (0,E) is the empty route (F7).
- Feasibility of "postpone everything" requires `P + R ≤ WB` and `W_k ≤ H^max_k` on entry.
- The MILP may place waiting before the break node; evaluator, brute force and checker must allow this too (F1, D-007 accepted).
- Pruning extensions and tighter C11 big-M: F4/F5, D-009 proposed. They are not yet part of the specification below.

**Amendments (2026-10-02)**
- **Fixed duty start (D-018).** A used driver's duty runs from the shift start $S_k$ to $t^E_k + R$. Prep $P$ is done in $[S_k, t^0_k]$; any time at the depot before departure is work. Hence C14 and C16 use $S_k$ in place of $t^0_k - P$, and $M^w_k = F_k - S_k$. Departure may still be later than $S_k + P$, but this no longer shortens the duty. An unused driver does not come in ($\theta_k = 0$).
- **Postponement (D-019).** $p_i$ is no longer a constant. On day $d$, for an order first due on day $d_i^0$: $p_i = p^{\text{base}} \cdot \rho^{\,d - d_i^0}$, and on the last day of the horizon $p_i = \max(p^{\text{base}} \cdot \rho^{\,d - d_i^0},\ p^{\text{end}})$, because postponing on that day leaves the order unserved this week. Values in `config/costs.yaml`.
- **Daytime duties, no night work (D-020).** Every shift lies in [05:00, 19:00] (`earliest_duty_start`, `latest_duty_end` in `config/rules.yaml`; $F_k = \min(S_k + 765, 19{:}00)$). Code des transports L3312-1 caps daily work at 10 h if a duty includes work between 00:00 and 05:00 or the driver is a night worker (≥ 50 h a month in the 21:00–06:00 night period). Neither can happen inside this window (≤ 1 h/day in the night period, ≈ 22 h/month), so the model needs no night constraint.
- **One trip per duty.** A route leaves the depot once and returns once; no reloading. This was implicit in A1; now explicit.

**Amendment V1-T8 (2026-10-04): breaks as the law states them (D-115 … D-117)**

V0 used one simplification of the break rules: a single break of $BR = 45$ min, or none. The law
is more permissive, and V1 follows it.

*Working time* (Directive 2002/15/EC art. 5; Code des transports L3312-2):

- **W1.** No more than $WB = 360$ consecutive minutes of work without a break.
- **W2.** Breaks total at least 30 min when the day's work is between 6 and 9 h, and at least
  45 min above 9 h.
- **W3.** Breaks may be split into parts of at least 15 min.

*Driving* (Regulation 561/2006 art. 7):

- **D1.** After at most $DB = 270$ min of driving, a break of 45 min, or a break of at least
  15 min followed later by one of at least 30 min. Such a break restarts the driving count.

Representation: a route carries an ordered list of breaks (order after whose service the break
starts, minutes). Work = duty minus breaks = $\theta$ (waiting is work, as before).

**Checker (the law, any list of breaks):**

- every break follows the service of an order of the route, in route order, at most one per
  order, and lasts ≥ 15 min (else `consistency`);
- work stretches between $S_k$, the breaks and $t^E + R$ are each ≤ $WB$ (`work_before_break`,
  `work_between_breaks`, `work_after_break`, or `work_without_break`);
- if there is a break: total breaks ≥ 45 when $\theta > 540$, ≥ 30 when $\theta > 360$
  (`break_too_short`);
- driving between qualifying breaks ≤ $DB$, where a qualifying break is ≥ 45 min, or ≥ 30 min after
  an earlier ≥ 15-min part since the last qualifying break (`drive_before_break`,
  `drive_between_breaks`, `drive_after_break`, or `drive_without_break`);
- $\theta = t^E + R - S_k - \sum$ breaks. The other rules are unchanged.

With one 45-min break this reduces exactly to the V0 checks, so every V0 label still holds.

**Planners** (evaluators, ALNS, enumeration, MILP) choose, per route, among four patterns.
`allow_short_break` and `allow_split_break` in `rules.yaml` switch the new ones off, to
reproduce V0:

| pattern | breaks | conditions beyond W1 |
|---|---|---|
| none | — | $\text{drive} \le DB$ |
| full | 45 after $b$ | drive before / after $b$ ≤ $DB$ |
| short | 30 after $b$ | $\theta \le 540$ and $\text{drive} \le DB$ (a lone 30 min does not restart driving) |
| split | 15 after $i$, 30 after $j$, $i$ before $j$ | drive up to $j$ ≤ $DB$, after $j$ ≤ $DB$ |

The split only pays when the 15-min part falls in waiting time: waiting is paid work, a
break is not. The short break shortens the duty by 15 min. Neither changes $\theta$ by itself;
they make tight duties feasible or free time for one more order.

**MILP (both formulations).** The break binary $y_{ik}$ (full, 45) is joined by
$y^{30}_{ik}$ (short), $y^{15}_{ik}$ and $y^{30s}_{ik}$ (split), each with a break start
$a^{\kappa}_k$ tied to $T_{ik}+s_i$ as in C11. The split's second part also gets a driving
level $b^{30s}_k$ tied to $D_{ik}$ as in C12. With
$\text{brk}^{\kappa}_k = \sum_i y^{\kappa}_{ik}$:

- **one pattern:** $\text{brk}_k + \text{brk}^{30}_k + \text{brk}^{15}_k \le 1$, and
  $\text{brk}^{30s}_k = \text{brk}^{15}_k$;
- **at most one break per order:** $y + y^{30} + y^{15} + y^{30s} \le \text{visit}$;
- **break delay** in C6–C7: $45y + 30y^{30} + 15y^{15} + 30y^{30s}$; the big-M values still
  use 45, the longest break at one node;
- **driving (C13):** $b, b^{30s} \le DB$; $\ \text{drive} - b - b^{30s} \le DB$.
  This is $\text{drive} \le DB$ for the none and short patterns;
- **work (C14):**
  - full: as V0;
  - short: $a^{30} - S \le WB$ and $t^E + R - a^{30} - 30 \le WB$;
  - split: $a^{15} - S \le WB$, $\ a^{30s} - a^{15} - 15 \le WB$, $\ t^E + R - a^{30s} - 30 \le WB$,
    and order $a^{30s} \ge a^{15} + 15$;
  - none: $t^E + R - S \le WB$ when no pattern is used;
- **service (C16):** $\theta \ge t^E + R - S - 45\,\text{brk} - 30\,\text{brk}^{30} - 45\,\text{brk}^{15}$;
- **W2 for the short break:** $\theta \le 540 + (DS - 540)(1 - \text{brk}^{30})$;
- **strong knapsacks:**
  - work $\le WB(\text{used} + \text{brk} + \text{brk}^{30} + 2\,\text{brk}^{15})$;
  - drive $\le DB(1 + \text{brk} + \text{brk}^{15})$;
  - the shift knapsack counts the break minutes of the pattern;
  - a forced break is any pattern.

With both switches off, none of these variables exists and the V0 model is unchanged.

---

## The daily MILP (brief §6)

### 6.1 Sets and parameters

- $C$: orders of the day, including orders postponed from the previous day.
- Nodes: $0$ (depot departure), $E$ (depot return), $C$.
- $K$: drivers working that day. $C_k \subseteq C$: orders compatible with driver $k$.
- $A_k$: arcs for driver $k$: $(0,j)$ for $j\in C_k$; $(i,j)$ for $i \neq j \in C_k$; $(i,E)$ for $i \in C_k$; $(0,E)$ (the empty route).
- **Arc pruning (mandatory):** drop $(i,j)$ if $e_i + s_i + \tau_{ij} > l_j$.
- $\tau_{ij}$ minutes, $d_{ij}$ km, $[e_i, l_i]$ service-start window, $s_i$ = `service_mu`, $q_i$ pallets, $Q_k$ capacity.
- $S_k$ shift start, $F_k$ shift end max; $P$ = `depot_prep`, $R$ = `depot_close`.
- Rules: $DB = 270$, $WB = 360$, $BR = 45$, $DD = 540$, $DS = 720$, $WD = 3360$.
- State: $W_k$ service minutes worked this week, $V_k$ driving minutes this week.
- Contract: $H^{\text{thr}}_k$, $H^{\max}_k$; costs $c^{\text{km}}$, $c^{\text{reg}}_k$, $c^{\text{ext}}_k$, $c^{\text{fix}}_k$, $p_i$.

### 6.2 Variables

| Name in code | Math | Domain | Meaning |
|---|---|---|---|
| `x[i,j,k]` | $x_{ijk}$ | binary | driver $k$ travels arc $(i,j)$ |
| `u[i]` | $u_i$ | binary | order $i$ postponed |
| `T[i,k]` | $T_{ik}$ | $[e_i, l_i]$ | service start at $i$ if $k$ visits it |
| `D[i,k]` | $D_{ik}$ | $[0, DD]$ | driving accumulated on arrival at $i$ |
| `y[i,k]` | $y_{ik}$ | binary | $k$ takes the break right after serving $i$ |
| `t0[k]` | $t^0_k$ | $[S_k + P,\ F_k]$ | departure from depot |
| `tE[k]` | $t^E_k$ | $[S_k + P,\ F_k - R]$ | return to depot |
| `a[k]` | $a_k$ | $[S_k, F_k]$ | break start |
| `b[k]` | $b_k$ | $[0, DD]$ | driving accumulated at the break |
| `svc[k]` | $\theta_k$ | $[0, DS]$ | *temps de service* today |
| `ext[k]` | $o_k$ | $\ge 0$ | weekly minutes above the contract threshold after today |

Expressions (not variables): $\text{visit}_{ik} = \sum_{(j,i)\in A_k} x_{jik}$; $\text{used}_k = 1 - x_{0Ek}$; $\text{drive}_k = \sum_{(i,j)\in A_k}\tau_{ij}x_{ijk}$; $\text{brk}_k = \sum_i y_{ik}$.

### 6.3 Objective

$$\min\ \sum_{k}\Big[c^{\text{km}}\!\!\sum_{(i,j)\in A_k}\! d_{ij}x_{ijk} + c^{\text{reg}}_k\,\theta_k + c^{\text{ext}}_k\,o_k + c^{\text{fix}}_k\,\text{used}_k\Big] + \sum_{i\in C} p_i\,u_i$$

### 6.4 Constraints

| # | Name in code | Constraint | For |
|---|---|---|---|
| C1 | `cover` | $\sum_{k: i\in C_k}\text{visit}_{ik} + u_i = 1$ | $i \in C$ |
| C2 | `leave` | $\sum_{j:(0,j)\in A_k} x_{0jk} = 1$ | $k$ |
| C3 | `flow` | $\sum_{j}x_{jik} = \sum_{j}x_{ijk}$ | $k,\ i\in C_k$ |
| C4 | `cap` | $\sum_{i}q_i\,\text{visit}_{ik} \le Q_k$ | $k$ |
| C5 | `time_dep` | $T_{jk} \ge t^0_k + \tau_{0j} - M^{0}_{jk}(1-x_{0jk})$ | $k,\ (0,j)$ |
| C6 | `time_arc` | $T_{jk} \ge T_{ik} + s_i + BR\,y_{ik} + \tau_{ij} - M_{ij}(1-x_{ijk})$ | $k,\ (i,j)$, $i,j\in C_k$ |
| C7 | `time_ret` | $t^E_k \ge T_{ik} + s_i + BR\,y_{ik} + \tau_{iE} - M^{E}_{ik}(1-x_{iEk})$; and $t^E_k \ge t^0_k$ | $k,\ i$ |
| C8 | `drv_dep` | $\tau_{0j} - DD(1-x_{0jk}) \le D_{jk} \le \tau_{0j} + DD(1-x_{0jk})$ | $k,\ (0,j)$ |
| C9 | `drv_arc` | $D_{ik} + \tau_{ij} - (DD+\tau_{ij})(1-x_{ijk}) \le D_{jk} \le D_{ik} + \tau_{ij} + DD(1-x_{ijk})$ | $k,\ (i,j)$ |
| C10 | `brk_site` | $y_{ik} \le \text{visit}_{ik}$; $\ \text{brk}_k \le 1$ | $k,\ i$ |
| C11 | `brk_time` | $T_{ik}+s_i - M^{a}_{ik}(1-y_{ik}) \le a_k \le T_{ik}+s_i + M^{a}_{ik}(1-y_{ik})$ | $k,\ i$ |
| C12 | `brk_drive` | $D_{ik} - DD(1-y_{ik}) \le b_k \le D_{ik} + DD(1-y_{ik})$; $\ b_k \le DD\,\text{brk}_k$ | $k,\ i$ |
| C13 | `drive_seg` | $b_k \le DB$; $\ \text{drive}_k - b_k \le DB$ | $k$ |
| C14 | `work_seg` | $a_k - S_k \le WB + M^{w}_k(1-\text{brk}_k)$; $\ (t^E_k + R) - (a_k + BR) \le WB + M^{w}_k(1-\text{brk}_k)$; $\ (t^E_k + R) - S_k \le WB + M^{w}_k\,\text{brk}_k$ *(fixed start, D-018)* | $k$ |
| C15 | `drive_day` | $\text{drive}_k \le DD$ | $k$ |
| C16 | `svc_def` | $\theta_k \ge (t^E_k + R) - S_k - BR\,\text{brk}_k - M^{w}_k(1-\text{used}_k)$ *(fixed start, D-018)* | $k$ |
| C17 | `week_caps` | $W_k + \theta_k \le H^{\max}_k$; $\ V_k + \text{drive}_k \le WD$ | $k$ |
| C18 | `extra` | $o_k \ge W_k + \theta_k - H^{\text{thr}}_k$ | $k$ |

**Why each piece is there** — keep these explanations in `docs/MODEL.md`:

- C6–C7 carry the break delay ($BR\,y_{ik}$) and also eliminate subtours, because every $s_i + \tau_{ij} > 0$. The generator must guarantee $s_i \ge 1$.
- C8–C9 are **equalities on used arcs**. With only the $\ge$ side, the solver could inflate $D_{ik}$ at the break node to make the after-break driving segment look shorter. Do not "simplify" them to one side.
- C11–C12 fix $a_k$ and $b_k$ to the values at the break node and are inactive elsewhere.
- C13 with $b_k = 0$ (no break) reduces to "a duty without a break drives ≤ 270".
- C14: the first two lines apply when there is a break, the third when there is not. Pre-trip $P$ and post-trip $R$ are work and belong to the first and last segment respectively; omitting $R$ accepts plans that are illegal by up to $R$ minutes.
- C16 makes an unused driver's service time 0, so unused drivers do not consume weekly hours.
- $\theta_k$ is bounded below only. It is never used as the reported value: **KPIs and the next day's state are recomputed from the extracted routes**, not read from model variables.

**Validation status.** This formulation, with the big-M values below, was checked against exhaustive enumeration on 42 random days ($n = 5$, $K = 2$) in three regimes (mixed, work-heavy, driving-heavy) with zero mismatches. Random instances alone did **not** detect a one-sided C9; the handcrafted trap of T6 does (illegal objective 742.00 against the correct 1 532.40). Treat the formulation as correct and the traps as mandatory.

### 6.5 Big-M values (compute per constraint; never use one global constant)

| Constraint | $M$ |
|---|---|
| C5 | $M^0_{jk} = \max(0,\ F_k + \tau_{0j} - e_j)$ |
| C6 | $M_{ij} = \max(0,\ l_i + s_i + BR + \tau_{ij} - e_j)$ |
| C7 | $M^E_{ik} = \max(0,\ l_i + s_i + BR + \tau_{iE} - S_k - P)$ |
| C11 | $M^a_{ik} = \max(F_k,\ l_i + s_i) - \min(S_k,\ e_i + s_i)$ |
| C14, C16 | $M^w_k = F_k - S_k$ (fixed start, D-018) |
| C8, C12, upper side of C9 | $DD$ |
| lower side of C9 | $DD + \tau_{ij}$ — with only $DD$, a visited node with high accumulated driving would wrongly constrain its successor through an *unused* arc |

### 6.6 Symmetry and warm start

- If two drivers are identical (same contract, truck capacity and tail-lift, shift, state), add $\sum_{i} \text{visit}_{ik} \ge \sum_{i} \text{visit}_{i,k+1}$ for consecutive identical drivers.
- Always pass the baseline plan (§7) as a MIP start through `Start` attributes on `x`, `y`, `u`, `t0`.

### 6.7 Gurobi usage

Parameters come from `config/solver.yaml`: `TimeLimit` (120 s per day for `small`), `MIPGap` (0.01), `Threads`, `Seed`, `OutputFlag`, `LogFile` (one log file per solve under `results/`). Name every variable and constraint with the code names above and their indices. For `tiny` instances, also write the model to `.lp`. If the status is `INFEASIBLE`, compute the IIS and write `.ilp` before raising. Record for every solve: status, runtime, objective, best bound, gap, node count, number of variables, constraints and non-zeros before and after presolve.

If the time limit is hit **with** an incumbent, accept it and report the gap. If it is hit **without** one, return the baseline plan and flag the day.

---

