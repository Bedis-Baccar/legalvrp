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
    // Break pattern (V1-T8): none, one full break, a lone short break, or the 15 + 30 split.
    const auto& br = rt.breaks;
    const bool ok = br.empty() || (br.size() == 1 && br[0].minutes == r.break_length) ||
                    (br.size() == 1 && short_on_ && br[0].minutes == r.short_break_length) ||
                    (br.size() == 2 && split_on_ && br[0].minutes == r.split_break_first &&
                     br[1].minutes == r.split_break_second);
    if (!ok) return false;
  }

  // Defaults: nothing used, everything postponed, idle drivers.
  for (auto& arcs : x_) {
    for (auto& [arc, v] : arcs) v.set(GRB_DoubleAttr_Start, 0.0);
  }
  const bool weekly = weekly_part();
  for (std::size_t i = 0; i < n && !weekly; ++i) u_[i].set(GRB_DoubleAttr_Start, 1.0);
  for (std::size_t k = 0; k < K; ++k) {
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.compat[k][i]) continue;
      y_[k][i].set(GRB_DoubleAttr_Start, 0.0);
      if (short_on_) y30_[k][i].set(GRB_DoubleAttr_Start, 0.0);
      if (split_on_) {
        y15_[k][i].set(GRB_DoubleAttr_Start, 0.0);
        y30s_[k][i].set(GRB_DoubleAttr_Start, 0.0);
      }
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
    if (short_on_) a30_[k].set(GRB_DoubleAttr_Start, S);
    if (split_on_) {
      a15_[k].set(GRB_DoubleAttr_Start, S);
      a30s_[k].set(GRB_DoubleAttr_Start, S);
      b30s_[k].set(GRB_DoubleAttr_Start, 0.0);
    }
    if (rt == nullptr || rt->order_ids.empty()) {  // idle
      x_[k].at({p.depot_out, p.depot_in}).set(GRB_DoubleAttr_Start, 1.0);
      t0_[k].set(GRB_DoubleAttr_Start, S + r.depot_prep);
      tE_[k].set(GRB_DoubleAttr_Start, S + r.depot_prep);
      a_[k].set(GRB_DoubleAttr_Start, S);
      b_[k].set(GRB_DoubleAttr_Start, 0.0);
      svc_[k].set(GRB_DoubleAttr_Start, 0.0);
      if (!weekly) ext_[k].set(GRB_DoubleAttr_Start, std::max(0, st.service_minutes_week - con.weekly_threshold));
      continue;
    }
    int prev = p.depot_out;
    Minutes driven = 0;
    Minutes break_minutes = 0;
    double a = S, b = 0.0;
    for (std::size_t pos = 0; pos < rt->order_ids.size(); ++pos) {
      const int i = order_index(rt->order_ids[pos]);
      const auto ui = static_cast<std::size_t>(i);
      x_[k].at({prev, i}).set(GRB_DoubleAttr_Start, 1.0);
      driven += p.tau(prev, i);
      if (!weekly) u_[ui].set(GRB_DoubleAttr_Start, 0.0);
      T(i, static_cast<int>(k)).set(GRB_DoubleAttr_Start, rt->service_starts[pos]);
      D(i, static_cast<int>(k)).set(GRB_DoubleAttr_Start, driven);
      for (std::size_t bi = 0; bi < rt->breaks.size(); ++bi) {
        const Break& bk = rt->breaks[bi];
        if (bk.after_order_id != rt->order_ids[pos]) continue;
        const double at = rt->service_starts[pos] + p.s[ui];
        break_minutes += bk.minutes;
        if (rt->breaks.size() == 2) {  // the split: 15 then 30
          if (bi == 0) {
            y15_[k][ui].set(GRB_DoubleAttr_Start, 1.0);
            a15_[k].set(GRB_DoubleAttr_Start, at);
          } else {
            y30s_[k][ui].set(GRB_DoubleAttr_Start, 1.0);
            a30s_[k].set(GRB_DoubleAttr_Start, at);
            b30s_[k].set(GRB_DoubleAttr_Start, driven);
          }
        } else if (bk.minutes == r.break_length) {
          y_[k][ui].set(GRB_DoubleAttr_Start, 1.0);
          a = at;
          b = driven;
        } else {  // a lone short break
          y30_[k][ui].set(GRB_DoubleAttr_Start, 1.0);
          a30_[k].set(GRB_DoubleAttr_Start, at);
        }
      }
      prev = i;
    }
    x_[k].at({prev, p.depot_in}).set(GRB_DoubleAttr_Start, 1.0);
    const double theta = rt->return_time + r.depot_close - S - break_minutes;
    t0_[k].set(GRB_DoubleAttr_Start, rt->departure);
    tE_[k].set(GRB_DoubleAttr_Start, rt->return_time);
    a_[k].set(GRB_DoubleAttr_Start, a);
    b_[k].set(GRB_DoubleAttr_Start, b);
    svc_[k].set(GRB_DoubleAttr_Start, theta);
    if (!weekly) ext_[k].set(GRB_DoubleAttr_Start, std::max(0.0, st.service_minutes_week + theta - con.weekly_threshold));
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
      Minutes brk = 0;  // the break after this order, if any (V1-T8 patterns)
      if (y_[k][ui].get(GRB_DoubleAttr_X) > 0.5) brk = r.break_length;
      if (short_on_ && y30_[k][ui].get(GRB_DoubleAttr_X) > 0.5) brk = r.short_break_length;
      if (split_on_ && y15_[k][ui].get(GRB_DoubleAttr_X) > 0.5) brk = r.split_break_first;
      if (split_on_ && y30s_[k][ui].get(GRB_DoubleAttr_X) > 0.5) brk = r.split_break_second;
      if (brk > 0) {
        rt.breaks.push_back({day_.orders[ui].id, brk});
        leave += brk;
      }
      prev = i;
    }
    rt.return_time = leave + p.tau(prev, p.depot_in);
    plan.routes.push_back(std::move(rt));
  }
  for (std::size_t i = 0; i < static_cast<std::size_t>(p.n); ++i) {
    const bool not_served = weekly_part() ? served_[i].getValue() < 0.5 : u_[i].get(GRB_DoubleAttr_X) > 0.5;
    if (not_served) plan.postponed_order_ids.push_back(day_.orders[i].id);
  }
  plan.objective = weekly_part() ? 0.0 : model_.get(GRB_DoubleAttr_ObjVal);  // weekly: set by the caller
  return plan;
}

}  // namespace legalvrp::model
