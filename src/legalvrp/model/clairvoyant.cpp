#include "legalvrp/model/clairvoyant.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>

#include "legalvrp/domain/day.hpp"
#include "legalvrp/model/milp_day.hpp"

namespace legalvrp::model {

ClairvoyantResult solve_week_clairvoyant(GRBEnv& env, const WeekInstance& week, const MilpOptions& o,
                                         const std::vector<DayPlan>* start) {
  const int D = week.days;
  GRBModel m(env);
  m.set(GRB_IntParam_OutputFlag, 0);
  GRBLinExpr objective = 0;

  // Day d: every order released on or before d, zero weekly state (linked below).
  std::vector<DayInstance> days;
  for (int d = 0; d < D; ++d) {
    std::vector<Order> earlier;
    for (const auto& ord : week.orders) {
      if (ord.day < d) earlier.push_back(ord);
    }
    days.push_back(make_day_instance(week, d, earlier));
  }
  std::vector<std::unique_ptr<MilpModel>> parts;
  for (int d = 0; d < D; ++d) {
    parts.push_back(std::make_unique<MilpModel>(m, days[static_cast<std::size_t>(d)], o.formulation,
                                                "d" + std::to_string(d) + ":", objective));
  }
  auto local = [&](int d, const std::string& id) -> int {
    const auto& ords = days[static_cast<std::size_t>(d)].orders;
    const auto it = std::ranges::find(ords, id, &Order::id);
    return it == ords.end() ? -1 : static_cast<int>(it - ords.begin());
  };

  // Orders: served at most once; postponement penalties exactly as in the rolling loop.
  for (const auto& ord : week.orders) {
    GRBLinExpr once = 0;
    GRBLinExpr served_by = 0;  // sum_{d = r}^{t} z
    for (int t = ord.day; t < D; ++t) {
      const int li = local(t, ord.id);
      if (li >= 0) {
        once += parts[static_cast<std::size_t>(t)]->served(li);
        served_by += parts[static_cast<std::size_t>(t)]->served(li);
      }
      const Euros pen = effective_postpone_penalty(week.costs, ord, t, D - 1);
      objective += pen * (1.0 - served_by);
    }
    m.addConstr(once <= 1, "once[" + ord.id + "]");
  }

  // Drivers: weekly caps and overtime on weekly totals.
  std::vector<GRBVar> over;
  for (const auto& drv : week.drivers) {
    const Contract& con = *std::ranges::find(week.contracts, drv.contract_class, &Contract::name);
    GRBLinExpr svc = 0, drive = 0;
    for (int d = 0; d < D; ++d) {
      const auto& ds = days[static_cast<std::size_t>(d)].drivers;
      const auto it = std::ranges::find(ds, drv.id, &Driver::id);
      if (it == ds.end()) continue;
      const int k = static_cast<int>(it - ds.begin());
      svc += parts[static_cast<std::size_t>(d)]->svc(k);
      drive += parts[static_cast<std::size_t>(d)]->drive(k);
    }
    m.addConstr(svc <= con.weekly_service_max, "week_svc[" + drv.id + "]");
    m.addConstr(drive <= week.rules.weekly_drive_max, "week_drv[" + drv.id + "]");
    over.push_back(m.addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS, "ext[" + drv.id + "]"));
    m.addConstr(over.back() >= svc - con.weekly_threshold, "extra[" + drv.id + "]");
    objective += con.cost_per_min_extra * over.back();
  }
  m.setObjective(objective, GRB_MINIMIZE);
  m.update();

  ClairvoyantResult res;
  if (start != nullptr && static_cast<int>(start->size()) == D) {
    res.start_accepted = true;
    for (int d = 0; d < D; ++d) {
      res.start_accepted = parts[static_cast<std::size_t>(d)]->set_start((*start)[static_cast<std::size_t>(d)]) &&
                           res.start_accepted;
    }
    if (res.start_accepted) {  // overtime start from the start's weekly service totals
      for (std::size_t k = 0; k < week.drivers.size(); ++k) {
        double svc = 0.0;
        for (int d = 0; d < D; ++d) {
          const auto& ds = days[static_cast<std::size_t>(d)].drivers;
          const auto it = std::ranges::find(ds, week.drivers[k].id, &Driver::id);
          if (it != ds.end()) {
            svc += parts[static_cast<std::size_t>(d)]->svc(static_cast<int>(it - ds.begin())).get(GRB_DoubleAttr_Start);
          }
        }
        const Contract& con =
            *std::ranges::find(week.contracts, week.drivers[k].contract_class, &Contract::name);
        over[k].set(GRB_DoubleAttr_Start, std::max(0.0, svc - con.weekly_threshold));
      }
    }
  }

  m.set(GRB_DoubleParam_TimeLimit, o.solver.time_limit);
  m.set(GRB_DoubleParam_MIPGap, o.solver.mip_gap);
  m.set(GRB_IntParam_Threads, o.solver.threads);
  m.set(GRB_IntParam_Seed, o.solver.seed);
  if (!o.log_dir.empty()) {
    std::filesystem::create_directories(o.log_dir);
    m.set(GRB_IntParam_LogToConsole, 0);
    m.set(GRB_IntParam_OutputFlag, 1);
    m.set(GRB_StringParam_LogFile, (o.log_dir / (o.tag + ".log")).string());
  }
  res.stats.num_vars = m.get(GRB_IntAttr_NumVars);
  res.stats.num_constrs = m.get(GRB_IntAttr_NumConstrs);
  res.stats.num_nz = m.get(GRB_IntAttr_NumNZs);
  m.optimize();
  const int status = m.get(GRB_IntAttr_Status);
  res.stats.status = status == GRB_OPTIMAL ? "OPTIMAL" : status == GRB_TIME_LIMIT ? "TIME_LIMIT"
                                                                                 : "STATUS_" + std::to_string(status);
  res.stats.runtime_s = m.get(GRB_DoubleAttr_Runtime);
  res.stats.node_count = m.get(GRB_DoubleAttr_NodeCount);
  res.stats.best_bound = m.get(GRB_DoubleAttr_ObjBound);
  if (m.get(GRB_IntAttr_SolCount) == 0) {
    throw std::runtime_error("clairvoyant MILP: no solution (" + res.stats.status + ")");
  }
  res.stats.objective = m.get(GRB_DoubleAttr_ObjVal);
  res.stats.gap = m.get(GRB_DoubleAttr_MIPGap);

  // Plans: postponed on day d = released by d and not served on any day <= d.
  std::set<std::string> served_so_far;
  for (int d = 0; d < D; ++d) {
    DayPlan p = parts[static_cast<std::size_t>(d)]->extract();
    for (const auto& r : p.routes) served_so_far.insert(r.order_ids.begin(), r.order_ids.end());
    p.postponed_order_ids.clear();
    for (const auto& ord : week.orders) {
      if (ord.day <= d && !served_so_far.contains(ord.id)) p.postponed_order_ids.push_back(ord.id);
    }
    p.solver_stats = res.stats;
    res.plans.push_back(std::move(p));
  }
  return res;
}

}  // namespace legalvrp::model
