// Fast exact route evaluation (V1-T1, D-102). Same semantics as RouteEvaluator::evaluate
// (docs/MODEL.md; fixed start D-018; waiting before the break D-007), no Bellman-Ford.
//
// With the duty start fixed at S, temps de service is minimised by the earliest return, and:
//  * before the break, starting as early as possible minimises the earliest break start; the
//    break start a may then be delayed by waiting at the break node (up to l_b + s_b);
//  * after the break, earliest service gives the earliest return tE(a), non-decreasing in a, with
//    tE(a) - a non-increasing; so the best a is the smallest one keeping work after the break
//    <= WB (D-007), and every other constraint is an upper bound on a or on tE.
// Path segments are represented by f(x) = max(x + d, r) on x <= L: the ready time after the
// segment when ready to leave its start at x. Composition stays in that form.
#include <algorithm>
#include <limits>
#include <vector>

#include "legalvrp/heuristics/route_eval.hpp"

namespace legalvrp::heuristics {

namespace {

constexpr long long kNeg = -(1LL << 50);
constexpr long long kPos = (1LL << 50);

struct Fn {
  long long d = 0;     // shift
  long long r = kNeg;  // floor (window opening + what follows)
  long long L = kPos;  // latest feasible input time; L < kNeg / 2 means empty
};

Fn compose(const Fn& f, const Fn& g) {  // g after f
  if (f.L <= kNeg / 2 || g.L <= kNeg / 2 || f.r > g.L) return Fn{0, kNeg, kNeg};
  return Fn{f.d + g.d, std::max(f.r + g.d, g.r), std::min(f.L, g.L - f.d)};
}

bool feasible(const Fn& f, long long x) { return f.L > kNeg / 2 && x <= f.L; }
long long apply(const Fn& f, long long x) { return std::max(x + f.d, f.r); }

}  // namespace

QuickEvaluation RouteEvaluator::quick(std::size_t k, std::span<const std::size_t> seq) const {
  const Driver& drv = day_.drivers[k];
  const DriverData& dd = drivers_[k];
  const Rules& r = day_.rules;
  const Contract& c = *dd.contract;
  const Matrix& m = day_.matrix;
  const long long S = drv.shift_start, F = drv.shift_end_max;
  const long long P = r.depot_prep, R = r.depot_close, BR = r.break_length, WB = r.work_before_break;

  QuickEvaluation out;
  auto cost_of = [&](Minutes theta, double km, bool used) {
    const Minutes over = std::max(0, dd.week_service + theta - c.weekly_threshold);
    return day_.costs.cost_per_km * km + c.cost_per_min_regular * theta + c.cost_per_min_extra * over +
           (used ? c.fixed_cost_if_used : 0.0);
  };
  const std::size_t n = seq.size();
  if (n == 0) {
    out.legal = true;
    out.cost = cost_of(0, 0.0, false);
    return out;
  }

  Pallets load = 0;
  for (const auto i : seq) {
    load += orders_[i].q;
    if (orders_[i].needs_tail_lift && !dd.tail_lift) return out;
  }
  if (load > dd.capacity) return out;

  // Legs, km, driving (same summation order as evaluate(): identical doubles).
  thread_local std::vector<long long> leg;
  leg.assign(n + 1, 0);
  double km = 0.0;
  long long drive_total = 0;
  std::size_t prev = depot_;
  for (std::size_t i = 0; i <= n; ++i) {
    const std::size_t node = i < n ? orders_[seq[i]].node : depot_;
    leg[i] = m.time(prev, node);
    km += m.dist(prev, node);
    drive_total += leg[i];
    prev = node;
  }
  if (drive_total > r.daily_drive_max || dd.week_driving + drive_total > r.weekly_drive_max) return out;

  // Prefix (ready at stop i, from the depot) and suffix (from ready at stop i to the depot).
  thread_local std::vector<Fn> pre, post;
  pre.assign(n, Fn{});
  post.assign(n, Fn{});
  auto step = [&](std::size_t i) {
    const OrderData& o = orders_[seq[i]];
    return Fn{leg[i] + o.s, static_cast<long long>(o.e) + o.s, static_cast<long long>(o.l) - leg[i]};
  };
  Fn acc{};
  for (std::size_t i = 0; i < n; ++i) pre[i] = acc = compose(acc, step(i));
  Fn back{leg[n], kNeg, kPos};
  for (std::size_t i = n; i-- > 0;) {
    post[i] = back;
    back = compose(step(i), back);  // after the loop: depot -> ... -> depot (unused)
  }

  const long long x0 = S + P;
  const long long t_last = F - R;
  const long long cap = std::min<long long>(r.daily_service_max, c.weekly_service_max - dd.week_service);
  long long best_theta = std::numeric_limits<long long>::max();
  int best_break = -2;

  // No break: the whole duty is one work segment and one driving segment.
  if (drive_total <= r.drive_before_break) {
    const Fn full = compose(pre[n - 1], post[n - 1]);
    if (feasible(full, x0)) {
      const long long tE = apply(full, x0);
      const long long theta = tE + R - S;
      if (tE <= t_last && theta <= WB && theta <= cap) {
        best_theta = theta;
        best_break = -1;
      }
    }
  }
  // Break after stop b.
  long long before = 0;
  for (std::size_t b = 0; b < n; ++b) {
    before += leg[b];
    if (before > r.drive_before_break) break;  // later breaks only drive more before
    if (drive_total - before > r.drive_before_break) continue;
    const Fn& f = pre[b];
    const Fn& g = post[b];
    if (!feasible(f, x0) || g.L <= kNeg / 2) continue;
    if (g.d + R > WB) continue;                                   // work after the break, at best
    const OrderData& o = orders_[seq[b]];
    const long long a = std::max(apply(f, x0), g.r + R - BR - WB);  // smallest legal break start
    const long long a_max = std::min({static_cast<long long>(o.l) + o.s, S + WB, g.L - BR});
    if (a > a_max) continue;
    const long long tE = std::max(a + BR + g.d, g.r);
    const long long theta = tE + R - S - BR;
    if (tE > t_last || theta > cap) continue;
    if (theta < best_theta) {
      best_theta = theta;
      best_break = static_cast<int>(b);
    }
  }
  if (best_break == -2) return out;

  out.legal = true;
  out.service_minutes = static_cast<Minutes>(best_theta);
  out.driving_minutes = static_cast<Minutes>(drive_total);
  out.km = km;
  out.cost = cost_of(out.service_minutes, km, true);
  out.break_after = best_break;
  return out;
}

}  // namespace legalvrp::heuristics
