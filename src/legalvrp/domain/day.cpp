#include "legalvrp/domain/day.hpp"

#include <algorithm>

namespace legalvrp {

Euros effective_postpone_penalty(const Costs& costs, const Order& order, int day, int last_day) {
  const int first_due = order.postponed_from.value_or(order.day);
  Euros p = order.postpone_penalty;
  for (int d = first_due; d < day; ++d) p *= costs.postpone_escalation;
  if (day >= last_day) p = std::max(p, costs.unserved_end_penalty);
  return p;
}

DayInstance make_day_instance(const WeekInstance& week, int day, const std::vector<Order>& carried,
                              const std::vector<DriverWeekState>& states) {
  DayInstance d;
  d.day = day;
  d.depot = week.depot;
  d.customers = week.customers;
  d.trucks = week.trucks;
  d.matrix = week.matrix;
  d.rules = week.rules;
  d.contracts = week.contracts;
  d.costs = week.costs;

  d.orders = carried;
  for (const auto& o : week.orders) {
    if (o.day == day) d.orders.push_back(o);
  }
  for (auto& o : d.orders) {
    o.postpone_penalty = effective_postpone_penalty(week.costs, o, day, week.days - 1);
  }
  for (const auto& drv : week.drivers) {
    if (std::ranges::find(drv.available_days, day) == drv.available_days.end()) continue;
    d.drivers.push_back(drv);
    const auto s = std::ranges::find(states, drv.id, &DriverWeekState::driver_id);
    d.states.push_back(s != states.end() ? *s : DriverWeekState{drv.id, 0, 0, std::nullopt});
  }
  return d;
}

}  // namespace legalvrp
