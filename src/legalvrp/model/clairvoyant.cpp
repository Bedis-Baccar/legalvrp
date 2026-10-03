#include "legalvrp/model/clairvoyant.hpp"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>

#include "legalvrp/domain/day.hpp"
#include "legalvrp/model/milp_day.hpp"

namespace legalvrp::model {

ClairvoyantResult solve_window(GRBEnv& env, const WeekInstance& week, const WindowSpec& w, const MilpOptions& o,
                               const std::vector<DayPlan>* start) {
  const int first = w.first_day;
  const int last = w.last_day < 0 ? week.days - 1 : std::min(w.last_day, week.days - 1);
  const int H = last - first + 1;
  if (H <= 0) throw std::invalid_argument("solve_window: empty window");
  GRBModel m(env);
  m.set(GRB_IntParam_OutputFlag, 0);
  GRBLinExpr objective = 0;

  // Orders of the window: carried (released before `first`, postponed_from set) + released in it.
  std::vector<Order> pending = w.carried;
  for (const auto& ord : week.orders) {
    if (ord.day >= first && ord.day <= last) pending.push_back(ord);
  }
  auto release = [&](const Order& ord) { return std::max(ord.postponed_from.value_or(ord.day), first); };

  // Day d: every pending order released on or before d; zero state (weekly terms linked below).
  std::vector<DayInstance> days;
  for (int d = first; d <= last; ++d) {
    std::vector<Order> avail;
    for (const auto& ord : pending) {
      if (release(ord) <= d) avail.push_back(ord);
    }
    DayInstance day = make_day_instance(week, d, avail);
    day.orders.erase(std::remove_if(day.orders.begin(), day.orders.end(),
                                    [&](const Order& x) { return release(x) > d; }),
                     day.orders.end());  // make_day_instance also appends day d's own orders: dedupe
    std::set<std::string> seen;
    day.orders.erase(std::remove_if(day.orders.begin(), day.orders.end(),
                                    [&](const Order& x) { return !seen.insert(x.id).second; }),
                     day.orders.end());
    days.push_back(std::move(day));
  }
  std::vector<std::unique_ptr<MilpModel>> parts;
  for (int h = 0; h < H; ++h) {
    parts.push_back(std::make_unique<MilpModel>(m, days[static_cast<std::size_t>(h)], o.formulation,
                                                "d" + std::to_string(first + h) + ":", objective));
  }
  auto local = [&](int h, const std::string& id) -> int {
    const auto& ords = days[static_cast<std::size_t>(h)].orders;
    const auto it = std::ranges::find(ords, id, &Order::id);
    return it == ords.end() ? -1 : static_cast<int>(it - ords.begin());
  };

  // Orders: served at most once; postponement penalties of the rolling loop on the real days
  // (escalation from the first due day, end-of-week penalty only on the week's last day).
  for (const auto& ord : pending) {
    Order base = ord;
    base.postponed_from = ord.postponed_from.value_or(ord.day);
    GRBLinExpr once = 0;
    GRBLinExpr served_by = 0;
    for (int t = release(ord); t <= last; ++t) {
      const int li = local(t - first, ord.id);
      if (li >= 0) {
        once += parts[static_cast<std::size_t>(t - first)]->served(li);
        served_by += parts[static_cast<std::size_t>(t - first)]->served(li);
      }
      objective += effective_postpone_penalty(week.costs, base, t, week.days - 1) * (1.0 - served_by);
    }
    m.addConstr(once <= 1, "once[" + ord.id + "]");
  }

  // Drivers: weekly caps and overtime on the weekly totals, from the state at the window start.
  std::vector<GRBVar> over;
  for (const auto& drv : week.drivers) {
    const Contract& con = *std::ranges::find(week.contracts, drv.contract_class, &Contract::name);
    const auto st = std::ranges::find(w.states, drv.id, &DriverWeekState::driver_id);
    const double W0 = st != w.states.end() ? st->service_minutes_week : 0.0;
    const double V0 = st != w.states.end() ? st->driving_minutes_week : 0.0;
    GRBLinExpr svc = 0, drive = 0;
    for (int h = 0; h < H; ++h) {
      const auto& ds = days[static_cast<std::size_t>(h)].drivers;
      const auto it = std::ranges::find(ds, drv.id, &Driver::id);
      if (it == ds.end()) continue;
      const int k = static_cast<int>(it - ds.begin());
      svc += parts[static_cast<std::size_t>(h)]->svc(k);
      drive += parts[static_cast<std::size_t>(h)]->drive(k);
    }
    m.addConstr(svc <= con.weekly_service_max - W0, "week_svc[" + drv.id + "]");
    m.addConstr(drive <= week.rules.weekly_drive_max - V0, "week_drv[" + drv.id + "]");
    over.push_back(m.addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS, "ext[" + drv.id + "]"));
    m.addConstr(over.back() >= W0 + svc - con.weekly_threshold, "extra[" + drv.id + "]");
    objective += con.cost_per_min_extra * over.back();
    const auto rs = std::ranges::find(w.reserve, drv.id, &std::pair<std::string, double>::first);
    if (rs != w.reserve.end() && rs->second > 0.0 && w.reserve_cost > 0.0) {
      GRBVar r = m.addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS, "rsv[" + drv.id + "]");
      m.addConstr(r >= W0 + svc - (con.weekly_service_max - rs->second), "reserve[" + drv.id + "]");
      objective += w.reserve_cost * r;
    }
  }
  m.setObjective(objective, GRB_MINIMIZE);
  m.update();

  ClairvoyantResult res;
  if (start != nullptr && static_cast<int>(start->size()) == H) {
    res.start_accepted = true;
    for (int h = 0; h < H; ++h) {
      res.start_accepted = parts[static_cast<std::size_t>(h)]->set_start((*start)[static_cast<std::size_t>(h)]) &&
                           res.start_accepted;
    }
    if (res.start_accepted) {  // overtime start from the start's weekly service totals
      for (std::size_t k = 0; k < week.drivers.size(); ++k) {
        const auto st = std::ranges::find(w.states, week.drivers[k].id, &DriverWeekState::driver_id);
        double svc = st != w.states.end() ? st->service_minutes_week : 0.0;
        for (int h = 0; h < H; ++h) {
          const auto& ds = days[static_cast<std::size_t>(h)].drivers;
          const auto it = std::ranges::find(ds, week.drivers[k].id, &Driver::id);
          if (it != ds.end()) {
            svc += parts[static_cast<std::size_t>(h)]->svc(static_cast<int>(it - ds.begin())).get(GRB_DoubleAttr_Start);
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
    throw std::runtime_error("window MILP: no solution (" + res.stats.status + ")");
  }
  res.stats.objective = m.get(GRB_DoubleAttr_ObjVal);
  res.stats.gap = m.get(GRB_DoubleAttr_MIPGap);

  // Plans: postponed on day d = released by d and not served on any day <= d (in the window).
  std::set<std::string> served_so_far;
  for (int h = 0; h < H; ++h) {
    DayPlan p = parts[static_cast<std::size_t>(h)]->extract();
    for (const auto& r : p.routes) served_so_far.insert(r.order_ids.begin(), r.order_ids.end());
    p.postponed_order_ids.clear();
    for (const auto& ord : days[static_cast<std::size_t>(h)].orders) {
      if (!served_so_far.contains(ord.id)) p.postponed_order_ids.push_back(ord.id);
    }
    p.solver_stats = res.stats;
    res.plans.push_back(std::move(p));
  }
  return res;
}

ClairvoyantResult solve_week_clairvoyant(GRBEnv& env, const WeekInstance& week, const MilpOptions& o,
                                         const std::vector<DayPlan>* start) {
  return solve_window(env, week, WindowSpec{}, o, start);
}

}  // namespace legalvrp::model
