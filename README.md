# legalvrp — V0

Daily vehicle routing for a regional pallet distributor, with **legal driver hours**:
EU driving time (Reg. 561/2006), EU and French working time (Dir. 2002/15/EC,
Code des transports), shifts and weekly state. V0 plans a Monday–Friday week as five
daily MILPs solved exactly with Gurobi, checked by an independent legality checker.

- Specification: [PROJECT_BRIEF.md](PROJECT_BRIEF.md) · model: [docs/MODEL.md](docs/MODEL.md)
- Review and open findings: [docs/REVIEW_V0.md](docs/REVIEW_V0.md) · decisions: [docs/DECISIONS.md](docs/DECISIONS.md)
- Sizing and data sources: [docs/SIZING.md](docs/SIZING.md), [docs/DATA.md](docs/DATA.md)
- Model validation document: [docs/validation/model_validation.pdf](docs/validation/model_validation.pdf)

## Requirements

| | Version |
|---|---|
| C++ compiler | MSVC 19.50 (VS 2026 Build Tools, "Desktop development with C++"); GCC ≥ 13 on Linux |
| CMake / Ninja | ≥ 3.25 / any (Ninja ships with VS) |
| Gurobi | 12.0.x, **required**; `GUROBI_HOME` set; licence `%USERPROFILE%\gurobi.lic` (or `GRB_LICENSE_FILE`) |
| Network | first configure only (fetches pinned dependencies) |

On Windows the Gurobi C++ library is built for MSVC only. MinGW cannot link it.

## Build and test

```powershell
./scripts/build.ps1                        # Debug, all tests (unit + gurobi)
./scripts/build.ps1 -Preset msvc-release   # Release + LTO, for experiments
```

## Run (as tasks land)

```powershell
build/msvc-release/apps/legalvrp-generate --config config/instance_small.yaml --seed 1   # T2
build/msvc-release/apps/legalvrp-check    --instance data/instances/small/1 --plan plan.json  # T4
build/msvc-release/apps/legalvrp-run-week --instance data/instances/small/1               # T8
build/msvc-release/apps/legalvrp-scaling  --config config/instance_scale.yaml             # T9
```

## Layout

```
config/          rules, contracts, costs, solver, risk, instance_{tiny,small,scale}.yaml
src/legalvrp/    one static library per module
  domain/        records, rule/contract loaders, compatibility, paths     (T1)
  data/          portable RNG, synthetic generator, JSON I/O              (T2)
  estimate/      Estimator protocol, DeterministicEstimator               (T2)
  heuristics/    route evaluator, k-means, Hungarian, territory baseline  (T3, T5)
  check/         independent checker — links domain only                  (T4)
  model/         big-M, MILP builder, Gurobi wrapper, extraction          (T6, T7)
  week/          weekly state, rolling loop                               (T8)
  kpi/           KPIs and report                                          (T8)
apps/            legalvrp-{generate,check,run-week,scaling}
tests/unit/      Catch2 tests, labels: unit | gurobi
tests/fixtures/  duties.json, tiny_day.json, trap fixtures                (T4, T6)
docs/            MODEL, DECISIONS, REVIEW_V0, DATA, SIZING, validation/
data/ results/   generated, git-ignored
```

## Status

| Task | State |
|---|---|
| T0 skeleton, build, configs, CI | done |
| T1 domain records, config loaders with validation, compatibility | done |
| T2–T9 | to do, in order (see brief §12) |
