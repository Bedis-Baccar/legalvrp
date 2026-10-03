#include "legalvrp/week/loop.hpp"

#include <algorithm>

#include "legalvrp/domain/day.hpp"

namespace legalvrp::week {

WeekRun run_week(const WeekInstance& week, const DaySolver& solve) {
  WeekRun out;
  std::vector<DriverWeekState> states;
  for (const auto& d : week.drivers) states.push_back({d.id, 0, 0, std::nullopt});
  std::vector<Order> carried;

  for (int d = 0; d < week.days; ++d) {
    DayInstance day = make_day_instance(week, d, carried, states);
    DayPlan plan = solve(day);
    check::DayCheck dc = check::check_day(day, plan);

    for (const auto& f : dc.drivers) {  // weekly state from recomputed facts
      if (!f.used) continue;
      auto& st = *std::ranges::find(states, f.driver_id, &DriverWeekState::driver_id);
      st.service_minutes_week += f.service_minutes;
      st.driving_minutes_week += f.driving_minutes;
      st.last_duty_end = d * 24 * 60 + f.duty_end;
    }
    carried.clear();
    for (const auto& id : plan.postponed_order_ids) {
      Order o = *std::ranges::find(week.orders, id, &Order::id);
      o.postponed_from = o.postponed_from.value_or(o.day);
      carried.push_back(std::move(o));
    }
    out.days.push_back(std::move(day));
    out.day_checks.push_back(std::move(dc));
    out.plans.push_back(std::move(plan));
  }
  out.week_check = check::check_week(week, out.plans);
  return out;
}

}  // namespace legalvrp::week
