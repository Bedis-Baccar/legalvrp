#include "legalvrp/week/solve_day.hpp"

#include <algorithm>
#include <fstream>

#include "legalvrp/domain/json.hpp"
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

  if (options.warm_start) options.start = out.baseline;
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

namespace legalvrp::week {

void write_day_outputs(const std::filesystem::path& dir, const DaySolve& s) {
  using nlohmann::json;
  std::filesystem::create_directories(dir);
  const auto write = [&](const char* name, const json& j) {
    std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
    const std::string text = to_canonical_text(j);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  };
  write("plan.json", json(s.plan));
  json stats = json(s.milp.stats);
  stats["source"] = to_string(s.source);
  stats["objective_recomputed"] = s.check.objective;
  stats["baseline_objective"] = s.baseline_check.objective;
  stats["start_accepted"] = s.milp.start_accepted;
  stats["binaries"] = s.milp.binaries;
  stats["arcs"] = s.milp.arcs;
  stats["arcs_full"] = s.milp.arcs_full;
  stats["cuts_added"] = s.milp.cuts_added;
  if (s.milp.lp_bound) stats["lp_bound"] = *s.milp.lp_bound;
  write("stats.json", stats);
  write("violations.json", json(s.check.violations));
}

}  // namespace legalvrp::week
