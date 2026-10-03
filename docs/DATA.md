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

`truth.json` (V1-T6) is the generator's side of service times and is never read by a planner:
`service_true` (the week's realised minutes per order), `service_mean` and `service_sigma` (the
true distribution: lognormal, mean = fixed + per_pallet × pallets and cv by customer type, from
`service_true: [fixed, per_pallet, cv]` in the instance config). Planners use `service_mu` =
10 + 6 × pallets from `week.json`. The truth has its own RNG stream (`split(4)`), so it never
changes `week.json` or `matrix.json`. Without a `service_true` model the truth equals
`service_mu` with σ = 0 (V0). `legalvrp-robustness` executes plans with these truths
([ROBUSTNESS_V1.md](ROBUSTNESS_V1.md)).

## Using real data (T10: import path only, no fetching)

T10 as written in the brief (SIRENE download + OpenRouteService/OSRM matrices fetched by the
program) was **not implemented**: it needs network access, an API key that must be obtained
separately, multi-GB downloads and two heavy C++ dependencies (HTTP client, PROJ), and it does not
change any conclusion about the method. What it would bring — real customers and road travel
times — can be plugged in without code, by writing the two instance files from any source:

`week.json` (schema = `WeekInstance` in `src/legalvrp/domain/json.cpp`)
- `customers[]`: `id`, `name`, `type` (grocery | restaurant | site), `x_km`, `y_km` (any planar
  coordinates in km, used only by the baseline's k-means; for WGS84 use a local projection),
  optional `lat`, `lon`, `window_start`, `window_end` (minutes from midnight), `needs_tail_lift`.
- `orders[]`: `id`, `customer_id`, `day` (0 = Monday), `pallets`, `service_mu` (≥ 1),
  `service_sigma` (0), `postpone_penalty` (base, e.g. 200).
- `drivers[]`, `trucks[]`, `depot`, `rules`, `contracts`, `costs` (copy them from a generated
  instance and edit), `name`, `seed`, `days`, `certified`, `attempt`.

`matrix.json` (schema = `Matrix`)
- `node_ids`: the depot id first, then every customer id in the order of `customers[]`.
- `time_min`: square integer matrix of travel minutes (asymmetric allowed, zero diagonal) — e.g.
  an OSRM/ORS duration matrix converted to minutes, rounded up, + parking time per leg.
- `dist_km`: square matrix of road kilometres.

Validation: `data::validate_week` (ids, windows, s ≥ 1, matrix shape and integers) runs in the
tests; every tool then works unchanged (`legalvrp-run-week --instance <dir>`, `legalvrp-bench`,
`legalvrp-check`). Licences of the source data (SIRENE: Licence Ouverte 2.0; ORS terms) apply.
