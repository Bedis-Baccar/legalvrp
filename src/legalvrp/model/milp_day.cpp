#include "legalvrp/model/milp_day.hpp"

#include <algorithm>
#include <string>

namespace legalvrp::model {

namespace {
double dbl(Minutes m) { return static_cast<double>(m); }
}  // namespace

MilpModel::MilpModel(GRBEnv& env, const DayInstance& day, Formulation f)
    : day_(day), prep_(prepare(day, f)), model_(env) {
  build();
}

GRBVar& MilpModel::T(int i, int k) {
  return prep_.formulation == Formulation::strong ? Tn_[static_cast<std::size_t>(i)]
                                                  : Tk_[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)];
}
const GRBVar& MilpModel::T(int i, int k) const {
  return prep_.formulation == Formulation::strong ? Tn_[static_cast<std::size_t>(i)]
                                                  : Tk_[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)];
}
GRBVar& MilpModel::D(int i, int k) {
  return prep_.formulation == Formulation::strong ? Dn_[static_cast<std::size_t>(i)]
                                                  : Dk_[static_cast<std::size_t>(k)][static_cast<std::size_t>(i)];
}

void MilpModel::build() {
  const Prep& p = prep_;
  const Rules& r = day_.rules;
  const bool strong = p.formulation == Formulation::strong;
  const auto n = static_cast<std::size_t>(p.n);
  const auto K = static_cast<std::size_t>(p.K);
  const double DB = r.drive_before_break, WB = r.work_before_break, BR = r.break_length;
  const double DD = r.daily_drive_max, DS = r.daily_service_max, WD = r.weekly_drive_max;
  const double P = r.depot_prep, R = r.depot_close;
  const auto oid = [&](int v) -> std::string {
    if (v == p.depot_out) return "0";
    if (v == p.depot_in) return "E";
    return day_.orders[static_cast<std::size_t>(v)].id;
  };
  const auto kid = [&](std::size_t k) { return day_.drivers[k].id; };
  const auto ix = [](std::size_t a) { return static_cast<int>(a); };

  // ---------------------------------------------------------------- variables
  x_.assign(K, {});
  for (std::size_t k = 0; k < K; ++k) {
    for (const Arc& a : p.arcs[k]) {
      x_[k][{a.from, a.to}] = model_.addVar(0, 1, 0, GRB_BINARY,
                                            "x[" + oid(a.from) + "," + oid(a.to) + "," + kid(k) + "]");
      ++binaries_;
    }
  }
  for (std::size_t i = 0; i < n; ++i) {
    u_.push_back(model_.addVar(p.servable[i] ? 0.0 : 1.0, 1, 0, GRB_BINARY, "u[" + oid(ix(i)) + "]"));
    ++binaries_;
  }
  const auto hi_of = [&](std::size_t i) { return std::max(p.e[i], p.l[i]); };  // unservable: e > l
  if (strong) {
    for (std::size_t i = 0; i < n; ++i) {
      Tn_.push_back(model_.addVar(dbl(p.e[i]), dbl(hi_of(i)), 0, GRB_CONTINUOUS, "T[" + oid(ix(i)) + "]"));
      Dn_.push_back(model_.addVar(dbl(p.D_lb[i]), dbl(std::max(p.D_lb[i], p.D_ub[i])), 0, GRB_CONTINUOUS,
                                  "D[" + oid(ix(i)) + "]"));
    }
  } else {
    Tk_.assign(K, std::vector<GRBVar>(n));
    Dk_.assign(K, std::vector<GRBVar>(n));
    for (std::size_t k = 0; k < K; ++k) {
      for (std::size_t i = 0; i < n; ++i) {
        if (!p.compat[k][i]) continue;
        const std::string tag = "[" + oid(ix(i)) + "," + kid(k) + "]";
        Tk_[k][i] = model_.addVar(dbl(p.e[i]), dbl(hi_of(i)), 0, GRB_CONTINUOUS, "T" + tag);
        Dk_[k][i] = model_.addVar(0, DD, 0, GRB_CONTINUOUS, "D" + tag);
      }
    }
  }
  y_.assign(K, std::vector<GRBVar>(n));
  for (std::size_t k = 0; k < K; ++k) {
    const Driver& drv = day_.drivers[k];
    const double S = drv.shift_start, F = drv.shift_end_max;
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.compat[k][i]) continue;
      y_[k][i] = model_.addVar(0, 1, 0, GRB_BINARY, "y[" + oid(ix(i)) + "," + kid(k) + "]");
      ++binaries_;
    }
    t0_.push_back(model_.addVar(S + P, F - R, 0, GRB_CONTINUOUS, "t0[" + kid(k) + "]"));
    tE_.push_back(model_.addVar(S + P, F - R, 0, GRB_CONTINUOUS, "tE[" + kid(k) + "]"));
    a_.push_back(model_.addVar(S, F, 0, GRB_CONTINUOUS, "a[" + kid(k) + "]"));
    b_.push_back(model_.addVar(0, strong ? DB : DD, 0, GRB_CONTINUOUS, "b[" + kid(k) + "]"));
    svc_.push_back(model_.addVar(0, DS, 0, GRB_CONTINUOUS, "svc[" + kid(k) + "]"));
    ext_.push_back(model_.addVar(0, GRB_INFINITY, 0, GRB_CONTINUOUS, "ext[" + kid(k) + "]"));
  }

  // ---------------------------------------------------------------- expressions
  std::vector<std::vector<GRBLinExpr>> visit(K, std::vector<GRBLinExpr>(n)), out(K, std::vector<GRBLinExpr>(n));
  std::vector<GRBLinExpr> leave(K), drive(K), brk(K), used(K);
  std::map<std::pair<int, int>, GRBLinExpr> X;  // aggregated arc flow (strong)
  GRBLinExpr objective = 0;
  for (std::size_t k = 0; k < K; ++k) {
    const Contract& con = day_.contract(day_.drivers[k].contract_class);
    for (const Arc& a : p.arcs[k]) {
      GRBVar& v = x_[k].at({a.from, a.to});
      if (a.from == p.depot_out) leave[k] += v;
      if (a.to != p.depot_in) visit[k][static_cast<std::size_t>(a.to)] += v;
      if (a.from != p.depot_out) out[k][static_cast<std::size_t>(a.from)] += v;
      drive[k] += dbl(a.tau) * v;
      X[{a.from, a.to}] += v;
      objective += day_.costs.cost_per_km * a.km * v;
    }
    used[k] = 1.0 - x_[k].at({p.depot_out, p.depot_in});
    for (std::size_t i = 0; i < n; ++i) {
      if (p.compat[k][i]) brk[k] += y_[k][i];
    }
    objective += con.cost_per_min_regular * svc_[k] + con.cost_per_min_extra * ext_[k] +
                 con.fixed_cost_if_used * used[k];
  }
  for (std::size_t i = 0; i < n; ++i) objective += p.penalty[i] * u_[i];
  model_.setObjective(objective, GRB_MINIMIZE);

  // ---------------------------------------------------------------- C1-C4
  for (std::size_t i = 0; i < n; ++i) {
    GRBLinExpr cover = u_[i];
    for (std::size_t k = 0; k < K; ++k) cover += visit[k][i];
    model_.addConstr(cover == 1, "cover[" + oid(ix(i)) + "]");
  }
  for (std::size_t k = 0; k < K; ++k) {
    model_.addConstr(leave[k] == 1, "leave[" + kid(k) + "]");
    GRBLinExpr load = 0;
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.compat[k][i]) continue;
      model_.addConstr(visit[k][i] == out[k][i], "flow[" + oid(ix(i)) + "," + kid(k) + "]");
      load += dbl(p.q[i]) * visit[k][i];
    }
    const double Q = day_.truck(day_.drivers[k].truck_id).capacity_pallets;
    if (strong) {
      model_.addConstr(load <= Q * used[k], "cap[" + kid(k) + "]");
    } else {
      model_.addConstr(load <= Q, "cap[" + kid(k) + "]");
    }
  }

  // ---------------------------------------------------------------- C5-C9: time and driving
  for (std::size_t k = 0; k < K; ++k) {
    const Driver& drv = day_.drivers[k];
    const double S = drv.shift_start, F = drv.shift_end_max;
    for (const Arc& a : p.arcs[k]) {
      const GRBVar& v = x_[k].at({a.from, a.to});
      const double tau = a.tau;
      const std::string tag = "[" + oid(a.from) + "," + oid(a.to) + "," + kid(k) + "]";
      if (a.from == p.depot_out && a.to != p.depot_in) {  // C5 (and C8 per driver in reference)
        const auto j = static_cast<std::size_t>(a.to);
        const double M = strong ? big_m(F - R + tau - p.e[j]) : big_m(F + tau - p.e[j]);
        model_.addConstr(T(a.to, ix(k)) >= t0_[k] + tau - M * (1 - v), "time_dep" + tag);
        if (!strong) {
          model_.addConstr(D(a.to, ix(k)) >= tau - DD * (1 - v), "drv_dep_lo" + tag);
          model_.addConstr(D(a.to, ix(k)) <= tau + DD * (1 - v), "drv_dep_hi" + tag);
        }
      } else if (a.from != p.depot_out && a.to == p.depot_in) {  // C7
        const auto i = static_cast<std::size_t>(a.from);
        const double M = big_m(p.l[i] + p.s[i] + BR + tau - (S + P));
        model_.addConstr(tE_[k] >= T(a.from, ix(k)) + dbl(p.s[i]) + BR * y_[k][i] + tau - M * (1 - v),
                         "time_ret" + tag);
      } else if (a.from != p.depot_out && !strong) {  // C6, C9 per driver (reference)
        const auto i = static_cast<std::size_t>(a.from);
        const auto j = static_cast<std::size_t>(a.to);
        const double M = big_m(p.l[i] + p.s[i] + BR + tau - p.e[j]);
        model_.addConstr(T(a.to, ix(k)) >= T(a.from, ix(k)) + dbl(p.s[i]) + BR * y_[k][i] + tau - M * (1 - v),
                         "time_arc" + tag);
        model_.addConstr(D(a.to, ix(k)) >= D(a.from, ix(k)) + tau - (DD + tau) * (1 - v), "drv_arc_lo" + tag);
        model_.addConstr(D(a.to, ix(k)) <= D(a.from, ix(k)) + tau + DD * (1 - v), "drv_arc_hi" + tag);
      }
    }
    model_.addConstr(tE_[k] >= t0_[k], "time_ret0[" + kid(k) + "]");
  }
  if (strong) {  // C6, C8, C9 on the aggregated flow X
    for (auto& [arc, Xij] : X) {
      const auto [from, to] = arc;
      if (to == p.depot_in) continue;
      const auto j = static_cast<std::size_t>(to);
      const double tau = p.tau(from, to);
      const std::string tag = "[" + oid(from) + "," + oid(to) + "]";
      if (from == p.depot_out) {  // C8
        model_.addConstr(Dn_[j] >= tau - big_m(tau - p.D_lb[j]) * (1 - Xij), "drv_dep_lo" + tag);
        model_.addConstr(Dn_[j] <= tau + big_m(p.D_ub[j] - tau) * (1 - Xij), "drv_dep_hi" + tag);
        continue;
      }
      const auto i = static_cast<std::size_t>(from);
      GRBLinExpr Yi = 0;
      for (std::size_t k = 0; k < K; ++k) {
        if (p.compat[k][i]) Yi += y_[k][i];
      }
      const double M = big_m(p.l[i] + p.s[i] + BR + tau - p.e[j]);
      model_.addConstr(Tn_[j] >= Tn_[i] + dbl(p.s[i]) + BR * Yi + tau - M * (1 - Xij), "time_arc" + tag);
      model_.addConstr(Dn_[j] >= Dn_[i] + tau - big_m(p.D_ub[i] + tau - p.D_lb[j]) * (1 - Xij), "drv_arc_lo" + tag);
      model_.addConstr(Dn_[j] <= Dn_[i] + tau + big_m(p.D_ub[j] - p.D_lb[i] - tau) * (1 - Xij), "drv_arc_hi" + tag);
      if (from < to && X.contains({to, from})) {
        model_.addConstr(Xij + X.at({to, from}) <= 1, "two_cycle" + tag);
      }
    }
  }

  // ---------------------------------------------------------------- C10-C18 per driver
  for (std::size_t k = 0; k < K; ++k) {
    const Driver& drv = day_.drivers[k];
    const DriverWeekState& st = day_.state(drv.id);
    const Contract& con = day_.contract(drv.contract_class);
    const double S = drv.shift_start, F = drv.shift_end_max;
    const double b_ub = strong ? DB : DD;
    const std::string K_ = "[" + kid(k) + "]";
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.compat[k][i]) continue;
      const std::string tag = "[" + oid(ix(i)) + "," + kid(k) + "]";
      const GRBVar& y = y_[k][i];
      model_.addConstr(y <= visit[k][i], "brk_site" + tag);                                  // C10
      const double s = p.s[i];
      const double Ma_lo = strong ? big_m(p.l[i] + s - S) : std::max(F, p.l[i] + s) - std::min(S, p.e[i] + s);
      const double Ma_hi = strong ? big_m(F - p.e[i] - s) : Ma_lo;
      model_.addConstr(a_[k] >= T(ix(i), ix(k)) + s - Ma_lo * (1 - y), "brk_time_lo" + tag);  // C11
      model_.addConstr(a_[k] <= T(ix(i), ix(k)) + s + Ma_hi * (1 - y), "brk_time_hi" + tag);
      const double Mb_lo = strong ? big_m(p.D_ub[i]) : DD;
      const double Mb_hi = strong ? big_m(b_ub - p.D_lb[i]) : DD;
      model_.addConstr(b_[k] >= D(ix(i), ix(k)) - Mb_lo * (1 - y), "brk_drive_lo" + tag);    // C12
      model_.addConstr(b_[k] <= D(ix(i), ix(k)) + Mb_hi * (1 - y), "brk_drive_hi" + tag);
      if (strong) {
        model_.addConstr(visit[k][i] <= used[k], "visit_used" + tag);
        if (p.force_break[k][i]) model_.addConstr(visit[k][i] <= brk[k], "force_break" + tag);
      }
    }
    model_.addConstr(brk[k] <= 1, "brk_one" + K_);
    model_.addConstr(b_[k] <= b_ub * brk[k], "brk_drive_off" + K_);
    if (!strong) model_.addConstr(b_[k] <= DB, "drive_seg_before" + K_);                     // C13
    model_.addConstr(drive[k] - b_[k] <= DB, "drive_seg" + K_);
    if (strong) {
      model_.addConstr(drive[k] <= DB + DB * brk[k], "drive_break" + K_);
      // Duty knapsacks (D-034): work = prep + driving + service + waiting + close, and each
      // work segment is <= WB, so driving + service + P + R <= WB (used + brk); the duty fits
      // in the shift; temps de service is at least the work done.
      GRBLinExpr service = 0;
      for (std::size_t i = 0; i < n; ++i) {
        if (p.compat[k][i]) service += dbl(p.s[i]) * visit[k][i];
      }
      model_.addConstr(drive[k] + service + (P + R) * used[k] <= WB * (used[k] + brk[k]), "work_knap" + K_);
      model_.addConstr(drive[k] + service + BR * brk[k] + (P + R) * used[k] <= (F - S) * used[k],
                       "shift_knap" + K_);
      model_.addConstr(svc_[k] >= drive[k] + service + (P + R) * used[k], "svc_lb" + K_);
    }
    // C14 work segments, fixed start (D-018)
    const double Mw1 = strong ? big_m(F - S - WB) : F - S;
    const double Mw2 = strong ? big_m(F - S - BR - WB) : F - S;
    model_.addConstr(a_[k] - S <= WB + Mw1 * (1 - brk[k]), "work_before" + K_);
    model_.addConstr(tE_[k] + R - (a_[k] + BR) <= WB + Mw2 * (1 - brk[k]), "work_after" + K_);
    model_.addConstr(tE_[k] + R - S <= WB + Mw1 * brk[k], "work_nobreak" + K_);
    model_.addConstr(drive[k] <= DD, "drive_day" + K_);                                      // C15
    model_.addConstr(svc_[k] >= tE_[k] + R - S - BR * brk[k] - (F - S) * (1 - used[k]), "svc_def" + K_);  // C16
    model_.addConstr(svc_[k] <= con.weekly_service_max - st.service_minutes_week, "week_svc" + K_);  // C17
    model_.addConstr(drive[k] <= WD - st.driving_minutes_week, "week_drv" + K_);
    model_.addConstr(ext_[k] >= dbl(st.service_minutes_week) + svc_[k] - con.weekly_threshold, "extra" + K_);  // C18
  }

  // ---------------------------------------------------------------- symmetry (§6.6)
  for (std::size_t k = 0; k + 1 < K; ++k) {
    const Driver& d1 = day_.drivers[k];
    const Driver& d2 = day_.drivers[k + 1];
    const Truck& t1 = day_.truck(d1.truck_id);
    const Truck& t2 = day_.truck(d2.truck_id);
    const DriverWeekState& s1 = day_.state(d1.id);
    const DriverWeekState& s2 = day_.state(d2.id);
    const bool identical = d1.contract_class == d2.contract_class && d1.shift_start == d2.shift_start &&
                           d1.shift_end_max == d2.shift_end_max && t1.capacity_pallets == t2.capacity_pallets &&
                           t1.has_tail_lift == t2.has_tail_lift &&
                           s1.service_minutes_week == s2.service_minutes_week &&
                           s1.driving_minutes_week == s2.driving_minutes_week;
    if (!identical) continue;
    GRBLinExpr a = 0, b = 0;
    for (std::size_t i = 0; i < n; ++i) {
      a += visit[k][i];
      b += visit[k + 1][i];
    }
    model_.addConstr(a >= b, "sym[" + kid(k) + "," + kid(k + 1) + "]");
  }
  model_.update();
}

}  // namespace legalvrp::model

namespace legalvrp::model {

std::unique_ptr<ConnectivityCuts> MilpModel::make_connectivity_cuts(bool per_driver) const {
  std::vector<CutArc> arcs;
  for (int k = 0; k < prep_.K; ++k) {
    for (const auto& [arc, v] : x_[static_cast<std::size_t>(k)]) {
      if (arc.second == prep_.depot_in) continue;  // only arcs entering a customer
      const int from = arc.first == prep_.depot_out ? prep_.n : arc.first;
      arcs.push_back({from, arc.second, k, v});
    }
  }
  return std::make_unique<ConnectivityCuts>(prep_.n, prep_.K, std::move(arcs), per_driver);
}

}  // namespace legalvrp::model
