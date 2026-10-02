#pragma once
// legalvrp::domain — build one day's problem from a stored week.

#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp {

// Orders of `day` plus `carried` (postponed from earlier days), drivers available that
// day, and their week state (zero state for drivers missing from `states`).
[[nodiscard]] DayInstance make_day_instance(const WeekInstance& week, int day,
                                            const std::vector<Order>& carried = {},
                                            const std::vector<DriverWeekState>& states = {});

}  // namespace legalvrp
