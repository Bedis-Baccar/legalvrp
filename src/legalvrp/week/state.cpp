#include "legalvrp/week/state.hpp"

#include <algorithm>
#include <stdexcept>

namespace legalvrp::week {

std::vector<DriverWeekState> initial_states(const WeekInstance& week) {
  std::vector<DriverWeekState> out;
  for (const auto& d : week.drivers) out.push_back({d.id, 0, 0, std::nullopt});
  return out;
}

void update_states(std::vector<DriverWeekState>& states, const check::DayCheck& day_check, int day) {
  for (const auto& f : day_check.drivers) {
    if (!f.used) continue;
    const auto it = std::ranges::find(states, f.driver_id, &DriverWeekState::driver_id);
    if (it == states.end()) throw std::out_of_range("no week state for driver " + f.driver_id);
    it->service_minutes_week += f.service_minutes;
    it->driving_minutes_week += f.driving_minutes;
    it->last_duty_end = day * 24 * 60 + f.duty_end;
  }
}

std::vector<Order> carried_orders(const WeekInstance& week, const DayPlan& plan) {
  std::vector<Order> out;
  for (const auto& id : plan.postponed_order_ids) {
    const auto it = std::ranges::find(week.orders, id, &Order::id);
    if (it == week.orders.end()) throw std::out_of_range("postponed order not in the week: " + id);
    Order o = *it;
    o.postponed_from = o.postponed_from.value_or(o.day);
    out.push_back(std::move(o));
  }
  return out;
}

}  // namespace legalvrp::week
