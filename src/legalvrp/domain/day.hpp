#pragma once
// legalvrp::domain — build one day's problem from a stored week.

#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp {

// Penalty for postponing `order` on `day` (D-019):
//   base * escalation^(day - first_due), with first_due = postponed_from or order.day,
//   and at least costs.unserved_end_penalty on the horizon's last day.
// `base` is the order's stored postpone_penalty (from the generator).
[[nodiscard]] Euros effective_postpone_penalty(const Costs& costs, const Order& order, int day,
                                               int last_day);

// Orders of `day` plus `carried` (postponed from earlier days; pass the ORIGINAL week orders
// with postponed_from set, not a previous day's escalated copies), drivers available that
// day, and their week state (zero state for drivers missing from `states`).
// Every order's postpone_penalty is replaced by its effective penalty for that day.
[[nodiscard]] DayInstance make_day_instance(const WeekInstance& week, int day,
                                            const std::vector<Order>& carried = {},
                                            const std::vector<DriverWeekState>& states = {});

}  // namespace legalvrp
