# legalvrp

[![ci](https://github.com/Bedis-Baccar/legalvrp/actions/workflows/ci.yml/badge.svg)](https://github.com/Bedis-Baccar/legalvrp/actions/workflows/ci.yml)

**Vehicle routing with legal driver hours** for regional pallet distribution, in C++20 with Gurobi.

Every afternoon a distributor plans the next day: which driver serves which order, in which
sequence, where the legally required break is taken, and which orders are postponed. Plans must
respect truck capacity and tail-lift equipment, customer receiving windows, EU driving-time rules
(Reg. 561/2006), EU and French working-time rules (Dir. 2002/15/EC, Code des transports), each
driver's shift and the hours already worked this week. The objective is the cost of the day:
distance, overtime, agency drivers and postponements.

V0 plans a Monday–Friday week as five daily MILPs linked by the drivers' weekly state, and checks
every plan with an independent legality checker.

## Results (V0)

| | |
|---|---|
| **Correctness** | The daily MILP matches an exact enumeration on 20 random days and on two trap cases; every reported plan passes the independent checker (41-duty labelled corpus) |
| **Effectiveness** | Against a territory baseline (k-means + Hungarian + 2-opt): weekly cost **−36 %** on a 4-driver instance, **−25 %** on a 6-driver instance; fewer postponements, no order left unserved |
| **Tractability** | Optimal up to 12 orders/day in < 6 s; 4/5 days solved at 15–20 orders; median gap 11 % at 25 orders (300 s) — the compact model's frontier |
| **Formulation** | Strengthened model (node-indexed times, window reduction, duty knapsacks, per-driver connectivity cuts): 15–37 % gaps where the textbook three-index model stays at 53–89 % |
| **Myopia** | A clairvoyant weekly MILP shows the rolling plan is near-optimal most weeks (median 5.6 % above), but up to 45 % when a scarce skill sits with a driver near a weekly cap |

Details: [benchmark](docs/BENCHMARK_V0.md) · [scaling](docs/SCALING_V0.md) · [myopia](docs/MYOPIA_V0.md).

## How it works

```
instance ──► territory baseline ──► daily MILP (Gurobi, warm start) ──► extraction ──► checker ──► plan
   ▲              (k-means,              strong formulation +                              │
   │            Hungarian, 2-opt)        connectivity cuts                                  ▼
   └────────────────────── weekly state + postponed orders (recomputed by the checker) ◄───┘
```

- **Route evaluator** — for a fixed sequence, every timing rule (windows, shift, 6 h work
  segments, service caps) is a difference constraint: a simple temporal network solved by
  Bellman–Ford gives the exact cheapest legal schedule and break position.
- **Daily MILP** — the model of [`docs/MODEL.md`](docs/MODEL.md): routing, time, driving
  accumulation, one 45-min break at a customer, work segments, daily and weekly caps, overtime.
- **Independent checker** — written from the rules, links nothing but the data model; validates
  plans as reported and recomputes every KPI.
- **Instance generator** — seeded and byte-identical across compilers and platforms; instances are
  certified by playing the baseline over the week.

## Build

Requirements: CMake ≥ 3.25, Ninja, MSVC 19.50 (Windows) or GCC ≥ 13 (Linux), Gurobi 12.0.x with
`GUROBI_HOME` set and a valid licence. Dependencies (nlohmann/json, yaml-cpp, CLI11, Catch2) are
fetched and pinned by CMake on first configure.

```powershell
./scripts/build.ps1                        # Windows: Debug build + all tests
./scripts/build.ps1 -Preset msvc-release   # optimised build for experiments
```

```bash
cmake --preset ci-linux && cmake --build --preset ci-linux && ctest --preset ci-linux   # without Gurobi
```

## Usage

```powershell
legalvrp-generate --config config/instance_small.yaml --seed 1          # certified instance -> data/instances/small/1
legalvrp-run-week --instance data/instances/small/1                      # rolling week -> results/small_1_milp/
legalvrp-run-week --instance data/instances/small/1 --solver baseline    # current practice, for comparison
legalvrp-check    --instance data/instances/small/1 --plans results/small_1_milp
legalvrp-bench    --instance data/instances/small/1                      # formulations + MILP vs baseline
legalvrp-scaling  --config config/instance_scale.yaml                    # runtime and gap vs size
legalvrp-myopia   --config config/instance_myopia.yaml                   # rolling vs clairvoyant week
```

Executables are in `build/<preset>/apps/`. Each run writes, per day, `plan.json`, `stats.json`,
`violations.json` and `gurobi.log`, and per week `week_kpis.json` and `week_report.md`.
Real data can be used by writing `week.json` and `matrix.json` ([docs/DATA.md](docs/DATA.md)).

## Repository layout

```
apps/            command-line tools (generate, check, run-week, bench, scaling, myopia)
config/          rules, contracts, costs, solver parameters, instance generators (YAML)
src/legalvrp/    one static library per module
  domain/          records, config loaders, compatibility, JSON, day builder
  data/            portable RNG, synthetic generator, instance files
  estimate/        service-time estimator interface (deterministic in V0)
  heuristics/      exact route evaluator, k-means, Hungarian, territory baseline, enumeration
  check/           independent legality checker (depends on domain only)
  model/           preprocessing, MILP, connectivity cuts, solve wrapper, clairvoyant week
  week/            rolling loop, weekly state, day orchestration, certification
  kpi/             KPIs and week report
tests/           Catch2 unit tests and fixtures (labelled duties, golden instances)
docs/            model, decisions, specification, data, benchmarks
scripts/         build helper
```

## Documentation

See [docs/README.md](docs/README.md) for the full index, and [CONTRIBUTING.md](CONTRIBUTING.md)
for development rules.

## Status

V0 complete: tasks T0–T9 of the [specification](docs/PROJECT_BRIEF.md), T10 as an import path for
real data, T11 (clairvoyant week). V1 in progress on branch `v1` ([plan](docs/V1_PLAN.md)): large
instances, an O(n) exact route evaluator, ALNS (at least as good as the 300-s MILP on every tested
day from 25 orders, ~1 % from the best known in 30 s at 60–100 orders; [benchmark](docs/BENCHMARK_V1.md)).
