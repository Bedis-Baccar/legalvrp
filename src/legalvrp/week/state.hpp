#pragma once
// legalvrp::week — weekly driver state, updated ONLY from the checker's recomputed facts of
// the extracted routes (brief §13: "state updated from theta instead of recomputed routes"
// is a known failure mode).

#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::week {

[[nodiscard]] std::vector<DriverWeekState> initial_states(const WeekInstance& week);

// Adds today's service and driving minutes of every used driver and records the absolute end
// of its duty (minutes since Monday 00:00).
void update_states(std::vector<DriverWeekState>& states, const check::DayCheck& day_check, int day);

// The original orders behind today's postponed ids, marked with the day they were first due
// (escalating penalties are applied when the next day is built, D-019).
[[nodiscard]] std::vector<Order> carried_orders(const WeekInstance& week, const DayPlan& plan);

}  // namespace legalvrp::week
