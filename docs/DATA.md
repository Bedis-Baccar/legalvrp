# DATA.md — instance sources and licences

Full specification: `PROJECT_BRIEF.md` §5. Source survey and licences: `docs/SIZING.md` §3.

| Mode | Source | Licence | Task |
|---|---|---|---|
| A (default) | in-house synthetic generator, seeded, offline | — | T2 |
| B (optional) | SIRENE géolocalisée + SIRENE stock (NAF filter); ORS or OSRM matrices cached to `data/cache/` | Licence Ouverte 2.0; ORS terms | T10 |

Units: integer minutes, integer pallets; distances float km; money float €.
Output layout: `data/instances/<name>/<seed>/{week.json, matrix.json, truth.json}` (D-013).
Generate: `build/msvc-release/apps/legalvrp-generate --config config/instance_small.yaml --seed 1`.
Golden copies for tests: `tests/fixtures/instances/{tiny,small}/1` (D-015), certified (D-027).
`data/` and `results/` are git-ignored. External data is fetched only by an explicit command.
