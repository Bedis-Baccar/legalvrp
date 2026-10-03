#include "legalvrp/week/solve_day.hpp"

#include <algorithm>

#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"

namespace legalvrp::week {

const char* to_string(PlanSource s) noexcept {
  switch (s) {
    case PlanSource::milp: return "milp";
    case PlanSource::milp_retimed: return "milp_retimed";
    case PlanSource::baseline_no_incumbent: return "baseline_no_incumbent";
    case PlanSource::baseline_rejected: return "baseline_rejected";
  }
  return "?";
}

namespace {

// Same sequences, schedules from the exact evaluator. nullopt if a sequence is illegal.
std::optional<DayPlan> retime(const DayInstance& day, const DayPlan& plan) {
  const heuristics::RouteEvaluator ev(day);
  DayPlan out = plan;
  out.routes.clear();
  out.objective = 0.0;
  for (std::size_t k = 0; k < day.drivers.size(); ++k) {
    std::vector<std::size_t> seq;
    const auto r = std::ranges::find(plan.routes, day.drivers[k].id, &Route::driver_id);
    if (r != plan.routes.end()) {
      for (const auto& id : r->order_ids) {
        seq.push_back(static_cast<std::size_t>(std::ranges::find(day.orders, id, &Order::id) - day.orders.begin()));
      }
    }
    const auto e = ev.evaluate(k, seq);
    if (!e.legal) return std::nullopt;
    out.objective += e.cost;
    if (!seq.empty()) out.routes.push_back(e.route);
  }
  for (const auto& id : plan.postponed_order_ids) {
    out.objective += std::ranges::find(day.orders, id, &Order::id)->postpone_penalty;
  }
  return out;
}

}  // namespace

DaySolve solve_day(GRBEnv& env, const DayInstance& day, model::MilpOptions options) {
  DaySolve out;
  out.baseline = heuristics::territory_baseline(day);
  out.baseline_check = check::check_day(day, out.baseline);

  options.start = out.baseline;
  out.milp = model::solve_day_milp(env, day, options);

  auto accept = [&](DayPlan plan, PlanSource src) {
    plan.solver_stats = out.milp.stats;
    plan.solver_stats.baseline_fallback = src == PlanSource::baseline_no_incumbent || src == PlanSource::baseline_rejected;
    out.check = check::check_day(day, plan);
    plan.objective = out.check.objective;  // reported objective = recomputed from routes (§11)
    out.plan = std::move(plan);
    out.source = src;
  };

  if (!out.milp.plan) {
    accept(out.baseline, PlanSource::baseline_no_incumbent);
    return out;
  }
  if (check::check_day(day, *out.milp.plan).ok()) {
    accept(*out.milp.plan, PlanSource::milp);
    return out;
  }
  if (auto r = retime(day, *out.milp.plan); r && check::check_day(day, *r).ok()) {
    accept(std::move(*r), PlanSource::milp_retimed);
    return out;
  }
  accept(out.baseline, PlanSource::baseline_rejected);
  return out;
}

}  // namespace legalvrp::week
