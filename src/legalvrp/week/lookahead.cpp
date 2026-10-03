#include "legalvrp/week/lookahead.hpp"

#include <algorithm>
#include <exception>

#include "legalvrp/domain/compat.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/model/clairvoyant.hpp"
#include "legalvrp/week/solve_day.hpp"
#include "legalvrp/week/state.hpp"

namespace legalvrp::week {

const char* to_string(LookaheadSource s) noexcept {
  switch (s) {
    case LookaheadSource::window: return "window";
    case LookaheadSource::window_retimed: return "window_retimed";
    case LookaheadSource::myopic: return "myopic";
    case LookaheadSource::fallback: return "fallback";
  }
  return "?";
}

std::vector<std::pair<std::string, double>> scarce_reserve(const WeekInstance& week, int last_known_day) {
  std::vector<std::pair<std::string, double>> out;
  const int after = week.days - 1 - last_known_day;
  if (after <= 0) return out;
  const auto& mx = week.matrix;
  const std::size_t depot = mx.index_of(week.depot.id);
  std::vector<double> minutes(week.drivers.size(), 0.0);
  for (const auto& ord : week.orders) {
    if (ord.day > last_known_day) continue;
    const Customer& c = *std::ranges::find(week.customers, ord.customer_id, &Customer::id);
    std::size_t only = week.drivers.size();
    int able = 0;
    for (std::size_t k = 0; k < week.drivers.size(); ++k) {
      const Truck& t = *std::ranges::find(week.trucks, week.drivers[k].truck_id, &Truck::id);
      if (compatible(t, c, ord)) {
        ++able;
        only = k;
      }
    }
    if (able != 1) continue;
    const std::size_t ci = mx.index_of(c.id);
    minutes[only] += static_cast<double>(mx.time(depot, ci) + ord.service_mu + mx.time(ci, depot));
  }
  for (std::size_t k = 0; k < week.drivers.size(); ++k) {
    if (minutes[k] > 0.0) {
      out.emplace_back(week.drivers[k].id, minutes[k] / static_cast<double>(last_known_day + 1) * after);
    }
  }
  return out;
}

LookaheadDay solve_day_lookahead(GRBEnv& env, const WeekInstance& week, const DayInstance& day,
                                 const DayContext& ctx, const LookaheadOptions& o) {
  LookaheadDay out;
  const DaySolve myopic = solve_day(env, day, o.day);
  out.myopic_objective = myopic.check.objective;
  out.plan = myopic.plan;
  out.source = LookaheadSource::myopic;

  const int last = std::min(day.day + o.horizon - 1, week.days - 1);
  model::WindowSpec spec;
  spec.first_day = day.day;
  spec.last_day = last;
  spec.states = ctx.states;
  spec.carried = ctx.carried;
  if (o.reserve_cost > 0.0) {
    spec.reserve = scarce_reserve(week, last);
    spec.reserve_cost = o.reserve_cost;
    out.reserve = spec.reserve;
  }
  if (last == day.day && spec.reserve.empty()) return out;  // nothing beyond the myopic day
  out.source = LookaheadSource::fallback;  // until the window succeeds

  // Start: the myopic day, then the baseline of each later day on the resulting carry-over/state.
  std::vector<DayPlan> start{myopic.plan};
  std::vector<DriverWeekState> states = ctx.states;
  update_states(states, myopic.check, day.day);
  std::vector<Order> carried = carried_orders(week, myopic.plan);
  for (int d = day.day + 1; d <= last; ++d) {
    const DayInstance next = make_day_instance(week, d, carried, states);
    DayPlan p = heuristics::territory_baseline(next);
    update_states(states, check::check_day(next, p), d);
    carried = carried_orders(week, p);
    start.push_back(std::move(p));
  }

  try {
    const auto res = model::solve_window(env, week, spec, o.window, &start);
    out.window_stats = res.stats;
    DayPlan plan = res.plans.front();
    if (check::check_day(day, plan).ok()) {
      out.source = LookaheadSource::window;
    } else if (auto r = retime_plan(day, plan); r && check::check_day(day, *r).ok()) {
      plan = std::move(*r);
      out.source = LookaheadSource::window_retimed;
    } else {
      return out;
    }
    plan.objective = check::check_day(day, plan).objective;
    plan.solver_stats = res.stats;
    out.plan = std::move(plan);
  } catch (const GRBException&) {    // Gurobi error: keep the myopic plan
  } catch (const std::exception&) {  // no incumbent (not expected: the start is feasible)
  }
  return out;
}

}  // namespace legalvrp::week
