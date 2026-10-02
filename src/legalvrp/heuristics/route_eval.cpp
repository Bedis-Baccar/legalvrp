#include "legalvrp/heuristics/route_eval.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>

namespace legalvrp::heuristics {

namespace {

constexpr long long kInf = std::numeric_limits<long long>::max() / 4;

// Simple temporal network: variables x_0..x_{n-1}; edge (u, v, w) means x_v - x_u <= w.
class Stn {
 public:
  explicit Stn(int n) : n_(n) {}
  void le(int u, int v, long long w) { edges_.push_back({u, v, w}); }   // x_v - x_u <= w
  void ge(int u, int v, long long c) { edges_.push_back({v, u, -c}); }  // x_v - x_u >= c

  // Shortest distances from `src` (reversed: distances *to* src). nullopt on negative cycle.
  [[nodiscard]] std::optional<std::vector<long long>> distances(int src, bool reversed) const {
    std::vector<long long> d(static_cast<std::size_t>(n_), kInf);
    d[static_cast<std::size_t>(src)] = 0;
    for (int it = 0; it < n_; ++it) {
      bool changed = false;
      for (const auto& e : edges_) {
        const int a = reversed ? e.v : e.u;
        const int b = reversed ? e.u : e.v;
        const auto da = d[static_cast<std::size_t>(a)];
        if (da == kInf) continue;
        auto& db = d[static_cast<std::size_t>(b)];
        if (da + e.w < db) {
          db = da + e.w;
          changed = true;
        }
      }
      if (!changed) return d;
    }
    return std::nullopt;  // still relaxing after n passes: negative cycle = infeasible
  }

 private:
  struct Edge {
    int u, v;
    long long w;
  };
  int n_;
  std::vector<Edge> edges_;
};

// Which constraint families enter the network; used to diagnose infeasibility.
enum class Stage { schedule, work_before, work_after, work_all, daily_service, weekly_service };

struct Timing {
  Minutes t0 = 0;
  std::vector<Minutes> starts;
  Minutes tE = 0;
};

}  // namespace

RouteEvaluator::RouteEvaluator(const DayInstance& day) : day_(day) {
  depot_ = day.matrix.index_of(day.depot.id);
  for (const auto& d : day.drivers) {
    const Truck& t = day.truck(d.truck_id);
    const DriverWeekState& st = day.state(d.id);
    drivers_.push_back({t.capacity_pallets, t.has_tail_lift, &day.contract(d.contract_class),
                        st.service_minutes_week, st.driving_minutes_week});
  }
  for (const auto& o : day.orders) {
    const Customer& c = day.customer(o.customer_id);
    orders_.push_back({day.matrix.index_of(c.id), c.window_start, c.window_end, o.service_mu,
                       o.pallets, c.needs_tail_lift});
  }
}

RouteEvaluation RouteEvaluator::evaluate(std::size_t k, std::span<const std::size_t> seq) const {
  const Driver& drv = day_.drivers[k];
  const DriverData& dd = drivers_[k];
  const Rules& r = day_.rules;
  const Contract& c = *dd.contract;
  const Matrix& m = day_.matrix;
  const Minutes S = drv.shift_start;
  const Minutes F = drv.shift_end_max;
  const Minutes P = r.depot_prep;
  const Minutes R = r.depot_close;
  const Minutes BR = r.break_length;

  RouteEvaluation out;
  out.route.driver_id = drv.id;
  for (const auto i : seq) out.route.order_ids.push_back(day_.orders[i].id);

  auto cost_of = [&](Minutes theta, double km, bool used) {
    const Minutes over = std::max(0, dd.week_service + theta - c.weekly_threshold);
    return day_.costs.cost_per_km * km + c.cost_per_min_regular * theta +
           c.cost_per_min_extra * over + (used ? c.fixed_cost_if_used : 0.0);
  };

  // ---- empty route: idle driver, theta = 0 (C16)
  if (seq.empty()) {
    out.legal = true;
    out.route.departure = S + P;
    out.route.return_time = S + P;
    out.cost = cost_of(0, 0.0, false);
    return out;
  }

  // ---- time-independent checks: equipment, capacity
  auto violate = [&](std::string_view rule, std::string order, double amount) {
    out.violations.push_back({std::string{rule}, drv.id, std::move(order), amount});
  };
  Pallets load = 0;
  for (const auto i : seq) {
    load += orders_[i].q;
    if (orders_[i].needs_tail_lift && !dd.tail_lift) violate(rule::tail_lift, day_.orders[i].id, 0);
  }
  if (load > dd.capacity) violate(rule::capacity, "", load - dd.capacity);
  if (!out.violations.empty()) return out;

  // ---- legs: leg[i] = travel into stop i (i < m), leg[m] = back to the depot
  const std::size_t n = seq.size();
  std::vector<Minutes> leg(n + 1);
  double km = 0.0;
  std::size_t prev = depot_;
  for (std::size_t i = 0; i <= n; ++i) {
    const std::size_t node = i < n ? orders_[seq[i]].node : depot_;
    leg[i] = m.time(prev, node);
    km += m.dist(prev, node);
    prev = node;
  }
  Minutes drive_total = 0;
  for (const auto x : leg) drive_total += x;

  // STN nodes: 0 = zero reference, 1 = t0, 2..n+1 = T_i, n+2 = tE.
  const int Z = 0, T0 = 1, TE = static_cast<int>(n) + 2;
  auto Tn = [](std::size_t i) { return static_cast<int>(i) + 2; };

  // brk = -1: no break; otherwise break right after seq[brk].
  auto build = [&](int brk, Stage stage) {
    Stn g(static_cast<int>(n) + 3);
    const bool has_break = brk >= 0;
    // shift: S + P <= t0;  tE <= F - R;  tE >= t0
    g.ge(Z, T0, S + P);
    g.le(Z, T0, F);
    g.le(Z, TE, F - R);
    g.ge(T0, TE, 0);
    // windows
    for (std::size_t i = 0; i < n; ++i) {
      g.ge(Z, Tn(i), orders_[seq[i]].e);
      g.le(Z, Tn(i), orders_[seq[i]].l);
    }
    // travel + service (+ break after the break node)
    g.ge(T0, Tn(0), leg[0]);
    for (std::size_t i = 0; i < n; ++i) {
      const Minutes done = orders_[seq[i]].s + (static_cast<int>(i) == brk ? BR : 0);
      if (i + 1 < n) {
        g.ge(Tn(i), Tn(i + 1), done + leg[i + 1]);
      } else {
        g.ge(Tn(i), TE, done + leg[n]);
      }
    }
    // work segments (C14), fixed duty start S (D-018)
    if (has_break) {
      const auto b = static_cast<std::size_t>(brk);
      const Minutes sb = orders_[seq[b]].s;
      if (stage != Stage::schedule && stage != Stage::work_after) {
        g.le(Z, Tn(b), S + r.work_before_break - sb);            // (T_b + s_b) - S <= WB
      }
      if (stage != Stage::schedule && stage != Stage::work_before) {
        g.le(Tn(b), TE, r.work_before_break - R + sb + BR);      // (tE + R) - (T_b + s_b + BR) <= WB
      }
    } else if (stage != Stage::schedule) {
      g.le(Z, TE, S + r.work_before_break - R);                  // (tE + R) - S <= WB
    }
    // service caps: theta = tE + R - S - BR*[break]
    const Minutes br_credit = has_break ? BR : 0;
    if (stage == Stage::daily_service || stage == Stage::weekly_service) {
      g.le(Z, TE, S + r.daily_service_max - R + br_credit);
    }
    if (stage == Stage::weekly_service) {
      g.le(Z, TE, S + c.weekly_service_max - dd.week_service - R + br_credit);
    }
    return g;
  };

  // Earliest return (= min theta); then latest departure; then earliest service starts.
  // Each step only fixes a variable inside its feasible interval, which an STN can always
  // extend to a full solution. nullopt if infeasible.
  auto solve = [&](int brk, Stage stage) -> std::optional<Timing> {
    Stn g = build(brk, stage);
    const auto to_z = g.distances(Z, true);  // d(v, Z); earliest x_v = -d(v, Z)
    if (!to_z) return std::nullopt;
    const long long te_min = -(*to_z)[static_cast<std::size_t>(TE)];
    g.le(Z, TE, te_min);
    const auto from_z = g.distances(Z, false);  // d(Z, v): latest x_v
    if (!from_z) return std::nullopt;
    const long long t0_late = (*from_z)[static_cast<std::size_t>(T0)];
    g.le(Z, T0, t0_late);
    g.ge(Z, T0, t0_late);
    const auto earliest = g.distances(Z, true);
    if (!earliest) return std::nullopt;
    Timing t;
    t.t0 = static_cast<Minutes>(t0_late);
    t.tE = static_cast<Minutes>(-(*earliest)[static_cast<std::size_t>(TE)]);
    for (std::size_t i = 0; i < n; ++i) {
      t.starts.push_back(static_cast<Minutes>(-(*earliest)[static_cast<std::size_t>(Tn(i))]));
    }
    return t;
  };

  struct Best {
    int brk = -2;
    Timing timing;
    Minutes theta = 0;
  };
  std::optional<Best> best;
  // Most nearly legal failing option, for the diagnosis: (progress, violations).
  int best_progress = -1;
  std::vector<Violation> best_failure;

  for (int brk = -1; brk < static_cast<int>(n); ++brk) {
    std::vector<Violation> fail;
    int progress = 0;
    const std::string brk_id = brk >= 0 ? day_.orders[seq[static_cast<std::size_t>(brk)]].id : "";

    // driving (time-independent): C13, C15, C17
    Minutes before = 0;
    if (brk >= 0) {
      for (int i = 0; i <= brk; ++i) before += leg[static_cast<std::size_t>(i)];
    }
    const Minutes after = drive_total - before;
    if (brk >= 0) {
      if (before > r.drive_before_break) {
        fail.push_back({std::string{rule::drive_before_break}, drv.id, brk_id, double(before - r.drive_before_break)});
      }
      if (after > r.drive_before_break) {
        fail.push_back({std::string{rule::drive_after_break}, drv.id, brk_id, double(after - r.drive_before_break)});
      }
    } else if (drive_total > r.drive_before_break) {
      fail.push_back({std::string{rule::drive_without_break}, drv.id, "", double(drive_total - r.drive_before_break)});
    }
    if (drive_total > r.daily_drive_max) {
      fail.push_back({std::string{rule::daily_drive_max}, drv.id, "", double(drive_total - r.daily_drive_max)});
    }
    if (dd.week_driving + drive_total > r.weekly_drive_max) {
      fail.push_back({std::string{rule::weekly_drive_max}, drv.id, "",
                      double(dd.week_driving + drive_total - r.weekly_drive_max)});
    }

    std::optional<Timing> timing;
    if (fail.empty()) {
      progress = 1;
      timing = solve(brk, Stage::weekly_service);  // everything
      if (!timing) {
        // Diagnose by adding constraint families one at a time.
        if (!solve(brk, Stage::schedule)) {
          fail.push_back({std::string{rule::schedule}, drv.id, brk_id, 0});
        } else if (brk >= 0 && !solve(brk, Stage::work_before)) {
          progress = 2;
          fail.push_back({std::string{rule::work_before_break}, drv.id, brk_id, 0});
        } else if (brk >= 0 && !solve(brk, Stage::work_after)) {
          progress = 2;
          fail.push_back({std::string{rule::work_after_break}, drv.id, brk_id, 0});
        } else if (!solve(brk, Stage::work_all)) {
          progress = 2;
          fail.push_back({std::string{brk >= 0 ? rule::work_after_break : rule::work_without_break},
                          drv.id, brk_id, 0});
        } else if (!solve(brk, Stage::daily_service)) {
          progress = 3;
          fail.push_back({std::string{rule::daily_service_max}, drv.id, brk_id, 0});
        } else {
          progress = 4;
          fail.push_back({std::string{rule::weekly_service_max}, drv.id, brk_id, 0});
        }
      }
    }

    if (timing) {
      const Minutes theta = timing->tE + R - S - (brk >= 0 ? BR : 0);
      if (!best || theta < best->theta) best = Best{brk, *timing, theta};  // ties: first option
    } else if (progress > best_progress) {
      best_progress = progress;
      best_failure = std::move(fail);
    }
  }

  if (!best) {
    out.violations = std::move(best_failure);
    return out;
  }

  // ---- assemble the legal route
  const Timing& t = best->timing;
  out.legal = true;
  out.service_minutes = best->theta;
  out.driving_minutes = drive_total;
  out.km = km;
  out.cost = cost_of(best->theta, km, true);
  Route& rt = out.route;
  rt.departure = t.t0;
  rt.service_starts = t.starts;
  rt.return_time = t.tE;
  if (best->brk >= 0) rt.break_after_order_id = day_.orders[seq[static_cast<std::size_t>(best->brk)]].id;
  Minutes ready = t.t0;  // time the driver leaves the previous node
  for (std::size_t i = 0; i < n; ++i) {
    rt.arrivals.push_back(ready + leg[i]);
    ready = t.starts[i] + orders_[seq[i]].s + (static_cast<int>(i) == best->brk ? BR : 0);
  }
  return out;
}

}  // namespace legalvrp::heuristics
