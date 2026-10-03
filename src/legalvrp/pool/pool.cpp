#include "legalvrp/pool/pool.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <set>

#include "legalvrp/heuristics/route_eval.hpp"

namespace legalvrp::pool {

Result solve_pool(GRBEnv& env, const DayInstance& day, const std::vector<alns::Column>& pool,
                  const std::vector<std::vector<std::size_t>>& incumbent_routes, const Options& o) {
  const auto t0 = std::chrono::steady_clock::now();
  const std::size_t n = day.orders.size();
  const std::size_t K = day.drivers.size();
  const heuristics::RouteEvaluator ev(day);

  // Columns: the pool plus the incumbent's routes, deduplicated; costs re-checked exactly.
  std::map<std::pair<std::size_t, std::vector<std::size_t>>, double> cols;
  for (const auto& c : pool) {
    if (!c.seq.empty()) cols.emplace(std::make_pair(c.driver, c.seq), c.cost);
  }
  Result res;
  for (std::size_t k = 0; k < K && k < incumbent_routes.size(); ++k) {
    if (incumbent_routes[k].empty()) continue;
    const auto q = ev.quick(k, incumbent_routes[k]);
    if (q.legal) cols[{k, incumbent_routes[k]}] = q.cost;
  }
  std::vector<double> idle(K);
  for (std::size_t k = 0; k < K; ++k) idle[k] = ev.quick(k, {}).cost;  // constant overtime if W > threshold

  GRBModel m(env);
  m.set(GRB_IntParam_OutputFlag, 0);
  m.set(GRB_DoubleParam_TimeLimit, o.time_limit_s);
  m.set(GRB_DoubleParam_MIPGap, o.mip_gap);
  m.set(GRB_IntParam_Threads, o.threads);
  std::vector<GRBVar> u(n);
  std::vector<GRBLinExpr> cover(n), per_driver(K);
  GRBLinExpr obj = 0;
  double constant = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    u[i] = m.addVar(0, 1, 0, GRB_BINARY, "u[" + day.orders[i].id + "]");
    cover[i] += u[i];
    obj += day.orders[i].postpone_penalty * u[i];
  }
  for (std::size_t k = 0; k < K; ++k) constant += idle[k];
  std::vector<std::pair<std::pair<std::size_t, std::vector<std::size_t>>, GRBVar>> x;
  x.reserve(cols.size());
  for (const auto& [key, cost] : cols) {
    GRBVar v = m.addVar(0, 1, 0, GRB_BINARY);
    obj += (cost - idle[key.first]) * v;
    for (const auto i : key.second) cover[i] += v;
    per_driver[key.first] += v;
    x.emplace_back(key, v);
  }
  for (std::size_t i = 0; i < n; ++i) m.addConstr(cover[i] == 1, "cover[" + day.orders[i].id + "]");
  for (std::size_t k = 0; k < K; ++k) m.addConstr(per_driver[k] <= 1, "one[" + day.drivers[k].id + "]");
  m.setObjective(obj + constant, GRB_MINIMIZE);
  m.update();

  // MIP start = the incumbent.
  std::set<std::size_t> served;
  for (auto& [key, v] : x) {
    const bool on = key.first < incumbent_routes.size() && incumbent_routes[key.first] == key.second;
    v.set(GRB_DoubleAttr_Start, on ? 1.0 : 0.0);
    if (on) served.insert(key.second.begin(), key.second.end());
  }
  res.incumbent_cost = constant;
  for (const auto& [key, v] : x) {
    if (key.first < incumbent_routes.size() && incumbent_routes[key.first] == key.second) {
      res.incumbent_cost += cols.at(key) - idle[key.first];
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    const bool post = !served.contains(i);
    u[i].set(GRB_DoubleAttr_Start, post ? 1.0 : 0.0);
    if (post) res.incumbent_cost += day.orders[i].postpone_penalty;
  }

  m.optimize();
  const int st = m.get(GRB_IntAttr_Status);
  res.status = st == GRB_OPTIMAL ? "OPTIMAL" : st == GRB_TIME_LIMIT ? "TIME_LIMIT" : "STATUS_" + std::to_string(st);
  res.columns = x.size();
  res.routes.assign(K, {});
  if (m.get(GRB_IntAttr_SolCount) > 0) {
    res.bound = m.get(GRB_DoubleAttr_ObjBound);
    for (const auto& [key, v] : x) {
      if (v.get(GRB_DoubleAttr_X) > 0.5) res.routes[key.first] = key.second;
    }
    for (std::size_t i = 0; i < n; ++i) {
      if (u[i].get(GRB_DoubleAttr_X) > 0.5) res.bank.push_back(i);
    }
    // Exact cost of the selected plan (no reliance on model arithmetic).
    res.cost = 0.0;
    for (std::size_t k = 0; k < K; ++k) res.cost += ev.quick(k, res.routes[k]).cost;
    for (const auto i : res.bank) res.cost += day.orders[i].postpone_penalty;
  } else {  // keep the incumbent
    res.routes = incumbent_routes;
    res.routes.resize(K);
    res.cost = res.incumbent_cost;
  }
  res.improved = res.cost < res.incumbent_cost - 1e-6;
  res.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  return res;
}

HybridResult solve_hybrid(GRBEnv& env, const DayInstance& day, const HybridOptions& o) {
  const auto t0 = std::chrono::steady_clock::now();
  HybridResult h;
  alns::Options first = o.alns;
  first.collect_pool = true;
  h.first = alns::solve(day, first);
  h.pool = solve_pool(env, day, h.first.pool, h.first.routes, o.pool);

  alns::Options polish = o.alns;
  polish.time_limit_s = o.polish_time_s;
  polish.max_iterations = 0;
  polish.start = h.pool.routes;
  polish.seed = o.alns.seed + 1;
  h.polish = alns::solve(day, polish);

  // Best of the three phases (the pool is never worse than the first phase; the polish starts
  // from the pool solution and keeps its best, so it is never worse either).
  const std::vector<std::vector<std::size_t>>* routes = &h.polish.routes;
  const std::vector<std::size_t>* bank = &h.polish.bank;
  h.cost = h.polish.cost;
  if (h.pool.cost < h.cost - 1e-9) {
    routes = &h.pool.routes;
    bank = &h.pool.bank;
    h.cost = h.pool.cost;
  }
  if (h.first.cost < h.cost - 1e-9) {
    routes = &h.first.routes;
    bank = &h.first.bank;
    h.cost = h.first.cost;
  }
  h.plan = alns::make_plan(day, *routes, *bank);
  h.plan.solver_stats.status = "ALNS+POOL";
  h.runtime_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  h.plan.solver_stats.runtime_s = h.runtime_s;
  h.plan.solver_stats.objective = h.cost;
  return h;
}

}  // namespace legalvrp::pool
