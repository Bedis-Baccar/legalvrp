#pragma once
// legalvrp::viz — the run bundle read by the visual application (docs/APP_PLAN.md, A1 + A2).
//
// One JSON document per planned week: the instance as the user should see it (customers,
// drivers, trucks, rules, contracts, costs), and per day the orders (with carried ones and their
// escalated penalties), the drivers' weekly state at the start of the day, every route with its
// timeline (viz::build_timeline), the postponed orders, the checker's verdict and the solver's
// statistics; then the week KPIs (kpi::compute_week_kpis).
//
// With the true service times (truth.json), every day is also executed as it really happens
// (sim::realise_day: same departures, sequences and breaks, true durations) and judged by the
// checker against the real instance; realised timelines, per-duty outcomes and realised week
// KPIs are added under "realised".
//
// Every figure comes from the checker or from the same walk as the checker: the application only
// draws.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/kpi/kpis.hpp"

namespace legalvrp::viz {

inline constexpr int kBundleSchema = 1;

struct RunData {
  std::string run;                           // run name (results/<run>)
  std::string solver;                        // alns, baseline, milp, hand, ...
  const WeekInstance* week = nullptr;        // planning instance: plans were made and checked on it
  const std::vector<DayInstance>* days = nullptr;
  const std::vector<DayPlan>* plans = nullptr;
  const std::vector<check::DayCheck>* day_checks = nullptr;
  const check::WeekCheck* week_check = nullptr;
  const kpi::WeekKpis* kpis = nullptr;
  // Optional: the real instance (before learned durations and time reserve; default: `week`)
  // and the true service minutes. With `truth`, the realised days are added.
  const WeekInstance* real_week = nullptr;
  const data::TrueService* truth = nullptr;
  nlohmann::json options = nlohmann::json::object();  // how the run was made (time limit, estimator, ...)
};

[[nodiscard]] nlohmann::json run_bundle(const RunData& run);

// A planned day judged against the real instance: the real rules, customers' windows, contracts
// and drivers' shifts (a plan made on a tightened instance, V1-T7, must be judged on reality).
[[nodiscard]] DayInstance judged_day(const DayInstance& planned, const WeekInstance& real);

}  // namespace legalvrp::viz
