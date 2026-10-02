# Decision log

Dated, append-only. Any change to the model goes to `docs/MODEL.md` first, then code, then here.
Status: **accepted**, **proposed** (needs the owner's sign-off), **superseded**.

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-001 | 2026-10-02 | **Implementation language is C++20** (CMake, MSVC on Windows, GCC in CI) instead of Python + gurobipy. Overrides PROJECT_BRIEF §2 ("C++ out of scope") and §10 | Owner's choice: one codebase from V0 to the V1 heuristics (ALNS) where C++ speed matters | accepted (owner) |
| D-002 | 2026-10-02 | Dependencies, pinned in `cmake/Dependencies.cmake`: nlohmann/json 3.12.0 (SHA-256 pinned), yaml-cpp 0.8.0, CLI11 2.5.0, Catch2 3.8.1; Gurobi 12.0.3 C++ API (installed locally). They replace pydantic/pyyaml/pytest. scipy (Hungarian) and scikit-learn (k-means) are replaced by in-house `heuristics/assignment` and `heuristics/kmeans` (≈ 150 lines, no dependency) | Minimal, header-light, widely used. Fetched at configure time only, never while solving or testing | accepted |
| D-003 | 2026-10-02 | **Portable RNG** (`data/rng`): a fixed generator (SplitMix64/PCG) plus in-house uniform, Poisson and normal sampling. `std::*_distribution` is forbidden in the generator | `std` distributions are implementation-defined. MSVC and libstdc++ give different draws for the same seed, which would break T2's "same seed ⇒ byte-identical output" between Windows and CI | accepted |
| D-004 | 2026-10-02 | **CI runs without Gurobi** (`ci-linux` preset, `-DLEGALVRP_WITH_GUROBI=OFF`, tests labelled `unit`). MILP tests (label `gurobi`) run locally under the academic licence | The pip restricted licence covers gurobipy only, not the C++ API, and the academic licence is tied to one host. Supersedes the brief's "CI runs MILP tests on tiny" | accepted |
| D-010 | 2026-10-02 | **Gurobi is mandatory for every local build.** There is no local no-Gurobi preset. `build.ps1` fails fast without `GUROBI_HOME` or a licence (`GRB_LICENSE_FILE`, default `%USERPROFILE%\gurobi.lic`). A test checks that the licence is not size-restricted (> 2000 variables) | Owner preference; `small`/`scale` need the full academic licence | accepted (owner) |
| D-005 | 2026-10-02 | Module boundaries are enforced **at link time**: one static library per module, `legalvrp::check` links `legalvrp::domain` only. `tests/unit/test_layering.cpp` scans `#include`s as a second guard | Stronger than an import test: a forbidden dependency is a build error | accepted |
| D-006 | 2026-10-02 | Scaling plots (T9): C++ writes CSV; figures come from a small optional script under `scripts/` | Keeps the C++ build free of plotting dependencies | accepted |
| D-007 | 2026-10-02 | Route evaluator (§7), brute force (T6) and checker (§8) must allow **waiting before the break node** (`T_b` anywhere in `[earliest, l_b]`). The checker validates the *reported* service starts rather than re-scheduling | Without it they reject plans the MILP legally produces. Counter-example in `docs/REVIEW_V0.md` F1. MILP unchanged | accepted (owner, option A) |
| D-008 | 2026-10-02 | **No access restrictions.** Any truck can deliver any customer. Removed: `Customer.access_class`, `Truck.size_class`, and the access penalty in `service_mu` (now `10 + 6 × pallets`). Compatibility = tail-lift if needed ∧ `pallets ≤ capacity`. Brief §1, §4, §5.1, §6.6 edited accordingly. The per-leg 4 min (parking/manoeuvring) is unrelated and kept | Owner's simplification. It also resolves review F2 (reversed access rule) | accepted (owner) |
| D-009 | 2026-10-02 | Extra arc pruning (review F4) and tighter C11 big-M (F5); complete MIP start (F6) | Tighter formulation and faster solves, valid under A7 | **proposed** |

## Owner decisions still open (from `docs/validation/model_validation.tex` §11)

D1 break model · D2 waiting as work · D3 cost of regular hours · D4 postponement
penalty (escalation?) · D5 part-time ceiling · D6 headline (rolling vs clairvoyant).
Current defaults in `config/`: D1 one break, D2 waiting = work, D3 tie-breaker 0.01 €/min,
D4 constant 200 €, D5 contract + 10 %, D6 rolling.

## T2 — instance generation (2026-10-02)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-011 | 2026-10-02 | **Order generation is count-first.** Day d gets `n_d = clamp(round(U{min..max} × weekday_factor[d]), min, max)` orders, placed on n_d distinct customers drawn without replacement with weight `order_weight` of their type. Brief §5.1 instead lets each customer order independently with probability p_type × factor, which only hits the 15–20 target on average | Guarantees the brief's orders-per-day range on every day, keeps Mon/Thu heavier, and is simpler than tuning probabilities | accepted |
| D-012 | 2026-10-02 | **Certification deferred to T5.** §5.1 asks the generator to certify each day with the baseline + checker (≤ 20 % postponed). Those come in T4–T5, so `WeekInstance.certified = false` until then; `max_postponed_share` is already in the config | Task order | accepted |
| D-013 | 2026-10-02 | Instance files are `week.json` + `matrix.json` + `truth.json` (no `.npz` in C++). Rules, contracts and costs are **embedded** in `week.json` | An instance stays reproducible if `config/` changes later; the checker needs nothing but the instance | accepted |
| D-014 | 2026-10-02 | Generator parameters chosen (illustrative): pallets λ grocery 2.0 / restaurant 0.5 / site 2.5; tail-lift need 15 % / 25 % / 0 %; order weight 1 / 1 / 0.6; type shares 40 / 40 / 20 %; customer pool = 2 × max orders per day. `small` gives ≈ 44 pallets/day against 60 pallets of fleet | The brief leaves these open; tuned so the fleet is loaded but not overloaded. To revisit when T5 certification measures postponements | accepted |
| D-015 | 2026-10-02 | Cross-platform byte identity is enforced by **golden fixtures** (`tests/fixtures/instances/{tiny,small}/1`), regenerated by the test on every build and compared byte for byte, and by RNG golden values from an independent implementation | MSVC and GCC verified identical locally; Linux CI repeats the check | accepted |

## T3 — route evaluator (2026-10-02)

| ID | Date | Decision | Why | Status |
|---|---|---|---|---|
| D-016 | 2026-10-02 | **The route evaluator solves a simple temporal network** instead of scanning departure minutes (brief §7). For a fixed sequence and break position, every timing rule (travel + service, windows, shift, the three 6 h work segments, daily and weekly service caps) is a difference constraint `x_v − x_u ≤ c`. Bellman–Ford gives feasibility, the minimum `tE − t0` (so the minimum temps de service, so the minimum cost), and an integer schedule achieving it. Driving limits are time-independent and checked directly. If no break position is legal, the evaluator reports the most nearly legal option's violations, by adding constraint families one at a time | Exact and polynomial (≈ 10⁵ operations per route); handles D-007 (waiting before the break) with no special case; the brief's departure scan alone cannot. Verified against a naive brute force (every departure minute × every break-node start) on 400 generated routes: 177 legal, all equal | accepted |
| D-017 | 2026-10-02 | Rule keys shared by the evaluator and the checker live in `domain/violation.hpp`. `make_day_instance` (week → day) lives in `domain/day.hpp` | Shared vocabulary only, no logic, so the checker stays independent of `heuristics/` | accepted |
