# Contributing

## Where things are decided

| Document | Role |
|---|---|
| [`docs/MODEL.md`](docs/MODEL.md) | The mathematics: single source of truth for the daily MILP |
| [`docs/DECISIONS.md`](docs/DECISIONS.md) | Dated log of every design and modelling decision (D-001 …) |
| [`docs/PROJECT_BRIEF.md`](docs/PROJECT_BRIEF.md) | Original V0 specification (task list T0–T11, acceptance criteria) |

A change to the model is made in `docs/MODEL.md` first, then in code, then logged in
`docs/DECISIONS.md` with its reason and how it was verified.

## Build and test

Requirements: CMake ≥ 3.25, Ninja, a C++20 compiler (MSVC 19.50 on Windows, GCC ≥ 13 on Linux),
Gurobi 12.0.x with `GUROBI_HOME` set and a licence (`GRB_LICENSE_FILE` or the default location).

```powershell
./scripts/build.ps1                        # Windows, MSVC Debug: configure + build + all tests
./scripts/build.ps1 -Preset msvc-release   # optimised + LTO, for experiments
./scripts/build.ps1 -Label unit            # skip the tests that need a Gurobi licence
```

```bash
cmake --preset ci-linux && cmake --build --preset ci-linux && ctest --preset ci-linux   # no Gurobi
```

Tests are Catch2 executables in `tests/unit/`, registered in `tests/CMakeLists.txt` with label
`unit` (runs everywhere, including CI) or `gurobi` (needs a licence; MILP tests).

## Rules of the code base

- **The checker is the judge.** No plan is reported, plotted or saved unless
  `legalvrp::check` returns zero violations. KPIs and weekly state are always recomputed by the
  checker from the extracted routes, never read from model variables.
- **Module boundaries are enforced by the build.** One static library per module;
  `legalvrp::check` links `legalvrp::domain` only, and `tests/unit/test_layering.cpp` scans
  `#include`s as a second guard. Do not add dependencies to the checker.
- **Units.** Integer minutes and integer pallets in instance data; money in euros (`double`).
- **No rule value as a literal in model code**: rules, contracts, costs and solver parameters come
  from `config/*.yaml`, which are validated strictly (errors name the file and the key).
- **Determinism.** The generator uses `data/rng` only (no `std::*_distribution`, no libm
  transcendentals), so instances are byte-identical on every platform; golden fixtures in
  `tests/fixtures/instances/` enforce it. Regenerate them deliberately after an intended change.
- **Dependencies** are pinned in `cmake/Dependencies.cmake`; add one only with a recorded decision.
- **No network** while solving or testing.

## Conventions

| Topic | Convention |
|---|---|
| Files | `src/legalvrp/<module>/<file>.{hpp,cpp}`, namespace `legalvrp::<module>`, included as `"legalvrp/<module>/<file>.hpp"` |
| Records | plain aggregates in `domain/models.hpp`, passed by const reference |
| Warnings | MSVC `/W4 /permissive-`; GCC `-Wall -Wextra -Wconversion …`, `-Werror` in CI |
| Formatting | `.clang-format` (Google style, 100 columns) |
| Commits | one task or decision per commit series; message says what changed and why |
