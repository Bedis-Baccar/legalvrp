// Solution transfer between plans and the MILP: complete MIP start (S6) and extraction.
#include "legalvrp/model/extract.hpp"

#include <algorithm>
#include <cmath>

#include "legalvrp/model/milp_day.hpp"

namespace legalvrp::model {

namespace {
Minutes as_minutes(double v) { return static_cast<Minutes>(std::llround(v)); }
}  // namespace

bool MilpModel::set_start(const DayPlan& plan) {
  const Prep& p = prep_;
  const Rules& r = day_.rules;
  const auto n = static_cast<std::size_t>(p.n);
  const auto K = static_cast<std::size_t>(p.K);
  const bool strong = p.formulation == Formulation::strong;
  auto order_index = [&](const std::string& id) {
    return static_cast<int>(std::ranges::find(day_.orders, id, &Order::id) - day_.orders.begin());
  };

  // Validate first: every arc used must exist, every served order must be servable.
  std::vector<int> route_of(K, -1);
  for (std::size_t ri = 0; ri < plan.routes.size(); ++ri) {
    const Route& rt = plan.routes[ri];
    const auto k = static_cast<std::size_t>(
        std::ranges::find(day_.drivers, rt.driver_id, &Driver::id) - day_.drivers.begin());
    if (k >= K) return false;
    route_of[k] = static_cast<int>(ri);
    int prev = p.depot_out;
    for (const auto& id : rt.order_ids) {
      const int i = order_index(id);
      if (i >= p.n || !x_[k].contains({prev, i})) return false;
      prev = i;
    }
    if (!x_[k].contains({prev, rt.order_ids.empty() ? p.depot_in : p.depot_in})) return false;
  }

  // Defaults: nothing used, everything postponed, idle drivers.
  for (auto& arcs : x_) {
    for (auto& [arc, v] : arcs) v.set(GRB_DoubleAttr_Start, 0.0);
  }
  for (std::size_t i = 0; i < n; ++i) u_[i].set(GRB_DoubleAttr_Start, 1.0);
  for (std::size_t k = 0; k < K; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.compat[k][i]) continue;
      y_[k][i].set(GRB_DoubleAttr_Start, 0.0);
      if (!strong) {
        Tk_[k][i].set(GRB_DoubleAttr_Start, p.e[i]);
        Dk_[k][i].set(GRB_DoubleAttr_Start, 0.0);
      }
    }
  }
  if (strong) {
    for (std::size_t i = 0; i < n; ++i) {
      Tn_[i].set(GRB_DoubleAttr_Start, p.e[i]);
      Dn_[i].set(GRB_DoubleAttr_Start, p.D_lb[i]);
    }
  }

  for (std::size_t k = 0; k < K; ++k) {
    const Driver& drv = day_.drivers[k];
    const DriverWeekState& st = day_.state(drv.id);
    const Contract& con = day_.contract(drv.contract_class);
    const double S = drv.shift_start;
    const Route* rt = route_of[k] >= 0 ? &plan.routes[static_cast<std::size_t>(route_of[k])] : nullptr;
    if (rt == nullptr || rt->order_ids.empty()) {  // idle
      x_[k].at({p.depot_out, p.depot_in}).set(GRB_DoubleAttr_Start, 1.0);
      t0_[k].set(GRB_DoubleAttr_Start, S + r.depot_prep);
      tE_[k].set(GRB_DoubleAttr_Start, S + r.depot_prep);
      a_[k].set(GRB_DoubleAttr_Start, S);
      b_[k].set(GRB_DoubleAttr_Start, 0.0);
      svc_[k].set(GRB_DoubleAttr_Start, 0.0);
      ext_[k].set(GRB_DoubleAttr_Start, std::max(0, st.service_minutes_week - con.weekly_threshold));
      continue;
    }
    int prev = p.depot_out;
    Minutes driven = 0;
    bool has_break = false;
    double a = S, b = 0.0;
    for (std::size_t pos = 0; pos < rt->order_ids.size(); ++pos) {
      const int i = order_index(rt->order_ids[pos]);
      const auto ui = static_cast<std::size_t>(i);
      x_[k].at({prev, i}).set(GRB_DoubleAttr_Start, 1.0);
      driven += p.tau(prev, i);
      u_[ui].set(GRB_DoubleAttr_Start, 0.0);
      T(i, static_cast<int>(k)).set(GRB_DoubleAttr_Start, rt->service_starts[pos]);
      D(i, static_cast<int>(k)).set(GRB_DoubleAttr_Start, driven);
      if (rt->break_after_order_id == rt->order_ids[pos]) {
        y_[k][ui].set(GRB_DoubleAttr_Start, 1.0);
        has_break = true;
        a = rt->service_starts[pos] + p.s[ui];
        b = driven;
      }
      prev = i;
    }
    x_[k].at({prev, p.depot_in}).set(GRB_DoubleAttr_Start, 1.0);
    const double theta = rt->return_time + r.depot_close - S - (has_break ? r.break_length : 0);
    t0_[k].set(GRB_DoubleAttr_Start, rt->departure);
    tE_[k].set(GRB_DoubleAttr_Start, rt->return_time);
    a_[k].set(GRB_DoubleAttr_Start, a);
    b_[k].set(GRB_DoubleAttr_Start, b);
    svc_[k].set(GRB_DoubleAttr_Start, theta);
    ext_[k].set(GRB_DoubleAttr_Start, std::max(0.0, st.service_minutes_week + theta - con.weekly_threshold));
  }
  return true;
}

DayPlan MilpModel::extract() const {
  const Prep& p = prep_;
  const Rules& r = day_.rules;
  DayPlan plan;
  plan.day = day_.day;
  for (std::size_t k = 0; k < static_cast<std::size_t>(p.K); ++k) {
    // Follow the arcs from the depot.
    std::vector<int> seq;
    int cur = p.depot_out;
    for (int guard = 0; guard <= p.n; ++guard) {
      int next = -1;
      for (const auto& [arc, v] : x_[k]) {
        if (arc.first == cur && v.get(GRB_DoubleAttr_X) > 0.5) {
          next = arc.second;
          break;
        }
      }
      if (next < 0 || next == p.depot_in) break;
      seq.push_back(next);
      cur = next;
    }
    if (seq.empty()) continue;

    Route rt;
    rt.driver_id = day_.drivers[k].id;
    rt.departure = as_minutes(t0_[k].get(GRB_DoubleAttr_X));
    Minutes leave = rt.departure;
    int prev = p.depot_out;
    for (const int i : seq) {
      const auto ui = static_cast<std::size_t>(i);
      rt.order_ids.push_back(day_.orders[ui].id);
      const Minutes arrival = leave + p.tau(prev, i);
      const Minutes start = std::max(arrival, as_minutes(T(i, static_cast<int>(k)).get(GRB_DoubleAttr_X)));
      rt.arrivals.push_back(arrival);
      rt.service_starts.push_back(start);
      leave = start + p.s[ui];
      if (y_[k][ui].get(GRB_DoubleAttr_X) > 0.5) {
        rt.break_after_order_id = day_.orders[ui].id;
        leave += r.break_length;
      }
      prev = i;
    }
    rt.return_time = leave + p.tau(prev, p.depot_in);
    plan.routes.push_back(std::move(rt));
  }
  for (std::size_t i = 0; i < static_cast<std::size_t>(p.n); ++i) {
    if (u_[i].get(GRB_DoubleAttr_X) > 0.5) plan.postponed_order_ids.push_back(day_.orders[i].id);
  }
  plan.objective = model_.get(GRB_DoubleAttr_ObjVal);
  return plan;
}

}  // namespace legalvrp::model
