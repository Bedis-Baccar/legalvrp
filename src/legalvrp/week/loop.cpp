#include "legalvrp/week/loop.hpp"

#include "legalvrp/domain/day.hpp"
#include "legalvrp/week/state.hpp"

namespace legalvrp::week {

WeekRun run_week(const WeekInstance& week, const DaySolver& solve) {
  WeekRun out;
  std::vector<DriverWeekState> states = initial_states(week);
  std::vector<Order> carried;

  for (int d = 0; d < week.days; ++d) {
    DayInstance day = make_day_instance(week, d, carried, states);
    DayPlan plan = solve(day);
    check::DayCheck dc = check::check_day(day, plan);
    update_states(states, dc, d);           // from recomputed facts, never from the plan
    carried = carried_orders(week, plan);   // original orders, first-due day kept
    out.days.push_back(std::move(day));
    out.day_checks.push_back(std::move(dc));
    out.plans.push_back(std::move(plan));
  }
  out.week_check = check::check_week(week, out.plans);
  return out;
}

}  // namespace legalvrp::week
