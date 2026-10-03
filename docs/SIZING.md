# `legalvrp` V0 — sizing, complexity projection, input data

Companion to [`PROJECT_BRIEF.md`](PROJECT_BRIEF.md) (the V0 specification).

---

## 1. What V0 is

One week, one depot, a handful of drivers, a few dozen orders, deterministic durations, **solved exactly with Gurobi**. No heuristic engine, no C++, no ML. Its purpose is to get the model right on instances small enough that the optimum is known, and to measure where exact solving stops being possible.

| | V0-tiny | **V0-small** (main) | V0-scale |
|---|---|---|---|
| Purpose | unit tests, CI | the demonstrable week | complexity measurement |
| Horizon | 1 day | **Mon–Fri, rolling** | 1 day |
| Drivers | 2 | **4** (3 full-time, 1 part-time) | $\lceil n/5 \rceil$ |
| Orders per day | 6–8 | **15–20** (≈ 90 a week) | 8 → 40 |
| Licence | pip (restricted) | academic | academic |

**The switch to Gurobi changes the method for V0, not the project.** At this size a compact MILP solved directly is the right tool: the optimum is certified, the model is the specification, and every later heuristic (ALNS, column generation) gets a ground truth to be compared against. Python + `gurobipy` only; C++ waits until a heuristic is needed.

---

## 2. Complexity projection

### 2.1 What is hard and what is not

- **Sequencing and assignment are hard.** The VRPTW is NP-hard, and with a fixed fleet even *finding a feasible plan* is strongly NP-complete (Savelsbergh 1985).
- **Legalising one route is easy.** For a fixed sequence, placing breaks under hours-of-service rules is polynomial — $O(n^3)$ in Archetti & Savelsbergh's trip-scheduling setting (2009). V0 restricts itself to one break placed at a customer, which makes the check a scan over at most $n$ positions.
- **The week adds state, not combinatorics,** in the rolling version: five independent daily problems linked by hours worked. The clairvoyant version — all five days in one model — is one problem five times larger.

### 2.2 How big the search space is

Plans for one day = ways to distribute $n$ orders into $K$ ordered routes $= \dfrac{(n+K-1)!}{(K-1)!}$, before breaks and postponements.

| Orders $n$ | Drivers $K$ | Candidate daily plans |
|---|---|---|
| 8 | 2 | $4 \times 10^{5}$ |
| 10 | 3 | $2 \times 10^{8}$ |
| 15 | 3 | $2 \times 10^{14}$ |
| 20 | 4 | $4 \times 10^{21}$ |
| 30 | 6 | $8 \times 10^{37}$ |

At $10^9$ evaluations per second, enumerating 20 orders and 4 drivers takes about 137 000 years. A V0-small week is $\approx 10^{108}$ plans. Branch-and-bound works because bounds discard almost all of it — which is precisely why the formulation's quality (tight big-M, arc pruning) matters more than the hardware.

### 2.3 Size of the MILP (formulation in the brief, before arc pruning)

| $n$ | $K$ | Binaries | Variables | Constraints | Fits pip licence? |
|---|---|---|---|---|---|
| 8 | 2 | 170 | 214 | 534 | yes |
| 10 | 3 | 373 | 451 | 1 165 | yes |
| 13 | 3 | 601 | 697 | 1 852 | yes — the limit |
| 15 | 3 | 783 | 891 | 2 400 | no |
| 20 | 4 | 1 784 | 1 968 | 5 440 | no |
| 30 | 6 | 5 796 | 6 192 | 17 580 | no |
| 40 | 8 | 13 488 | 14 176 | 40 800 | no |

Growth is $O(K n^2)$ in both variables and constraints. Weekly clairvoyant model at 15 orders/day and 4 drivers: ≈ 5 900 variables and 16 000 constraints.

**Licence consequence.** `pip install gurobipy` ships a restricted licence capped at 2 000 variables and 2 000 constraints (200 variables if the model has quadratic terms). That is enough for the test instances — so **CI can run real MILP tests without a licence** — but not for V0-small. Experiments need the free academic licence (register with your ENSTA address on Gurobi's academic programme).

### 2.4 Expected tractability on a laptop

Memory is not the constraint: a 15 000-variable model is a few megabytes. Time is. For big-M three-index VRPTW formulations, the usual picture is:

| Orders/day | Expected behaviour with a 60–300 s limit |
|---|---|
| ≤ 15 | optimal in seconds to a minute |
| 15–25 | optimal or a small residual gap; strongly dependent on window tightness |
| 25–40 | persistent gaps; the warm start does most of the work |
| > 40 | wrong tool — this is where ALNS (and column generation for bounds) take over |

These are expectations, not results. **V0-scale exists to replace this table with a measured one**: $n \in \{8, 10, 12, 15, 20, 25, 30, 40\}$, five seeds, recorded runtime, gap and node count. The $n$ at which the median gap at 300 s exceeds 1 % is the compact model's frontier on your machine, and the empirical justification for V1.

Three levers move that frontier more than anything else, and the brief requires all three: arc pruning (time windows, tail-lift, capacity), per-constraint tight big-M values, and a feasible warm start.

---

## 3. Input data — what exists and what to use

| Source | What it gives | Licence | Verdict for V0 |
|---|---|---|---|
| **Synthetic generator** (ours) | Clustered customers, travel times, orders, windows, service times, drivers, contracts — fully controlled, offline, seeded | — | **Default for V0.** Reproducible, no network, sizes on demand |
| **SIRENE géolocalisée** (INSEE, data.gouv.fr) | Every French establishment with SIRET, Lambert-93 coordinates, monthly updates; joinable to the SIRENE stock for the NAF activity code | Licence Ouverte 2.0 | **Best real source for customers.** Filter by NAF (restaurants 56.10A, food retail 47.11, construction 41/43) around a real depot |
| **OpenRouteService** | Travel-time and distance matrices; up to 3 500 origin × destination pairs per request (e.g. 50 × 50) | free with key | Enough for V0 in a few calls; cache once |
| **OSRM** (self-hosted, Docker + Geofabrik extract) | Unlimited matrices, offline after setup | open source | Alternative to ORS; car profile × a truck factor |
| **CNR indices** (Comité National Routier) | Reference cost indices for French road freight (driver, fuel, vehicle) | public | Source for cost parameters; industry guides put a 7.5–19 t rigid in regional distribution at roughly 1.40–2.00 €/km |
| **Amazon Last Mile Routing Challenge** | 9 184 real routes: coordinates, travel times, *planned* service times, windows, package sizes, actual driver sequence | CC BY-NC | Not for V0 (US vans). Useful in V1 for realistic stop structure |
| **LaDe** (Cainiao) | 10.7 M packages, 21 k couriers, *realised* accept and delivery event times | Apache-2.0 | V1 only: calibrating the *shape* of duration distributions for the ML layer |
| **Solomon / VRP-REP** | Classical VRPTW benchmarks | free | Sanity checks of the routing core; no driver layer |

**No public dataset contains driver rosters, contracts and hours worked.** That layer is always generated from the regulation and from contract parameters, which is fine — it is a set of rules, not an empirical distribution.

### Recommended path

1. **V0:** synthetic everything. The generator is part of the project and is what makes tests and scaling experiments possible.
2. **V0, final step (optional):** swap the synthetic geography for SIRENE customers around a real logistics zone (e.g. Rungis, Lyon-Corbas, Toulouse-Fondeyre) with an ORS or OSRM matrix cached to disk. Same instance format; only the geography module changes. This is the instance you show.
3. **V1:** add LaDe- and Amazon-calibrated duration distributions when the ML layer arrives.

---

## 4. Modelling simplifications specific to V0

Stated so they are visible, each conservative (it can reject a legal plan, never accept an illegal one):

- **One break per duty**, 45 minutes, taken at a customer right after service. Sufficient for both break rules because daily driving ≤ 9 h = 2 × 4 h 30 and daily work ≤ 12 h = 2 × 6 h. It forgoes en-route and split breaks.
- **Waiting counts as work** and never as a break.
- **A 45-minute break whenever work exceeds 6 h**, although the law asks for only 30 minutes between 6 and 9 hours.
- **Same shift start every day** for a given driver, so the 11 h daily rest holds automatically (≤ 12 h 45 on duty, ≥ 11 h 15 off). The week loop checks it anyway.
- **Deterministic service times**; the $\sigma$ fields exist and are zero.

---

## Sources

[Gurobi restricted licence](https://support.gurobi.com/hc/en-us/articles/29682074018833-What-does-Restricted-license-for-non-production-use-only-mean) · [Gurobi pricing and academic licensing](https://www.gurobi.com/product/pricing-and-licensing) · [SIRENE géolocalisation](https://www.data.gouv.fr/datasets/geolocalisation-des-etablissements-du-repertoire-sirene-pour-les-etudes-statistiques) · [SIRENE stock](https://www.data.gouv.fr/datasets/base-sirene-des-entreprises-et-de-leurs-etablissements-siren-siret) · [OpenRouteService restrictions](https://openrouteservice.org/restrictions/) · [CNR cost indices](https://www.otre.org/indices-cnr-couts-de-personnel-de-conduite-en-2025-dans-le-secteur-du-trm/) · [regional truck €/km guide](https://affretium.com/fr/guides/tarif-transport-routier-km) · [Amazon Last Mile data](https://www.math.uwaterloo.ca/tsp/amz/data.html) · [LaDe](https://huggingface.co/datasets/Cainiao-AI/LaDe) · [Archetti & Savelsbergh, The Trip Scheduling Problem](https://pubsonline.informs.org/doi/10.1287/trsc.1090.0278)
