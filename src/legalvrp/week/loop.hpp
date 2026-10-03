#pragma once
// legalvrp::week — rolling Monday-Friday loop (brief §8 algorithm; core of task T8).
// Any day solver can be plugged in (baseline, MILP, ...). Each day: build the DayInstance
// (carried ORIGINAL orders, escalating penalties D-019, weekly state), solve, check; update
// the weekly state from the checker's recomputed facts (never from the plan or the model);
// carry the postponed orders. At the end the whole week is checked again in week mode.

#include <functional>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::week {

using DaySolver = std::function<DayPlan(const DayInstance&)>;

// What the loop knows at the start of a day beyond the instance (V1-T5 policies): every driver's
// weekly state (the instance only holds the drivers available that day) and the carried orders as
// ORIGINAL week orders (postponed_from set).
struct DayContext {
  const std::vector<DriverWeekState>& states;
  const std::vector<Order>& carried;
};
using ContextSolver = std::function<DayPlan(const DayInstance&, const DayContext&)>;

struct WeekRun {
  std::vector<DayPlan> plans;
  std::vector<DayInstance> days;          // the instances actually solved
  std::vector<check::DayCheck> day_checks;
  check::WeekCheck week_check;
};

[[nodiscard]] WeekRun run_week(const WeekInstance& week, const DaySolver& solve);
[[nodiscard]] WeekRun run_week_with_context(const WeekInstance& week, const ContextSolver& solve);

}  // namespace legalvrp::week
