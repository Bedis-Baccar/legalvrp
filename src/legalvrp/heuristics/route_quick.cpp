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
// Break patterns (V1-T8, BreakPattern) are tried in the same order as evaluate(), with the same
// tie-breaking; the 15 + 30 split has the closed form given at its loop.
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
  bool found = false;
  BreakPattern best;
  auto consider = [&](long long theta, const BreakPattern& pat) {
    if (!found || theta < best_theta) {  // ties: the first option, as in evaluate()
      best_theta = theta;
      best = pat;
      found = true;
    }
  };

  // No break: the whole duty is one work segment and one driving segment.
  if (drive_total <= r.drive_before_break) {
    const Fn full = compose(pre[n - 1], post[n - 1]);
    if (feasible(full, x0)) {
      const long long tE = apply(full, x0);
      const long long theta = tE + R - S;
      if (tE <= t_last && theta <= WB && theta <= cap) consider(theta, BreakPattern{});
    }
  }
  // One break of `brk` minutes after stop b: the smallest legal break start (D-007).
  auto one_break = [&](std::size_t b, long long brk, long long theta_cap, const BreakPattern& pat) {
    const Fn& f = pre[b];
    const Fn& g = post[b];
    if (!feasible(f, x0) || g.L <= kNeg / 2) return;
    if (g.d + R > WB) return;                                      // work after the break, at best
    const OrderData& o = orders_[seq[b]];
    const long long a = std::max(apply(f, x0), g.r + R - brk - WB);  // smallest legal break start
    const long long a_max = std::min({static_cast<long long>(o.l) + o.s, S + WB, g.L - brk});
    if (a > a_max) return;
    const long long tE = std::max(a + brk + g.d, g.r);
    const long long theta = tE + R - S - brk;
    if (tE > t_last || theta > theta_cap) return;
    consider(theta, pat);
  };
  // Full break (BR) after stop b.
  long long before = 0;
  for (std::size_t b = 0; b < n; ++b) {
    before += leg[b];
    if (before > r.drive_before_break) break;  // later breaks only drive more before
    if (drive_total - before > r.drive_before_break) continue;
    one_break(b, BR, cap, BreakPattern::full(static_cast<int>(b), r.break_length));
  }

  // V1-T8 patterns, tried in evaluate()'s order and only if they can still help.
  long long work_lb = drive_total + P + R;
  for (const auto i : seq) work_lb += orders_[i].s;
  auto may_help = [&] { return !found || best_theta > work_lb; };
  // A lone short break: only when the day's driving needs no break; work <= short_break_work_max.
  if (r.allow_short_break && drive_total <= r.drive_before_break && may_help()) {
    const long long cap_short = std::min<long long>(cap, r.short_break_work_max);
    for (std::size_t b = 0; b < n; ++b) {
      one_break(b, r.short_break_length, cap_short, BreakPattern::short_one(static_cast<int>(b), r.short_break_length));
    }
  }
  // Split: m1 after stop i, then m2 after stop j > i (restarts driving). With a1, a2 the break
  // starts: a2 is minimal at max(A1 + m1 + M.d, M.r, g.r + R - m2 - WB) (M: the path i -> j); if the
  // middle stretch a2 - a1 - m1 would exceed WB, a1 waits at i (a2 is unchanged since M.d <= WB).
  if (r.allow_split_break && may_help()) {
    const long long m1 = r.split_break_first, m2 = r.split_break_second;
    for (std::size_t i = 0; i + 1 < n; ++i) {
      const Fn& f = pre[i];
      if (!feasible(f, x0)) break;  // later stops are not reachable either
      const long long A1 = apply(f, x0);
      const OrderData& oi = orders_[seq[i]];
      const long long U1_base = std::min(static_cast<long long>(oi.l) + oi.s, S + WB);
      Fn M{0, kNeg, kPos};
      long long drive_j = 0;
      for (std::size_t k2 = 0; k2 <= i; ++k2) drive_j += leg[k2];
      for (std::size_t j = i + 1; j < n; ++j) {
        M = compose(M, step(j));
        drive_j += leg[j];
        if (M.L <= kNeg / 2 || M.d > WB) break;      // the middle only gets longer
        if (drive_j > r.drive_before_break) break;     // driving before the 30-min part
        if (drive_total - drive_j > r.drive_before_break) continue;
        const Fn& g = post[j];
        if (g.L <= kNeg / 2 || g.d + R > WB) continue;
        const long long a2 = std::max({A1 + m1 + M.d, M.r, g.r + R - m2 - WB});
        const long long a1 = std::max(A1, a2 - m1 - WB);
        if (a1 > std::min(U1_base, M.L - m1)) continue;
        const OrderData& oj = orders_[seq[j]];
        if (a2 > std::min(static_cast<long long>(oj.l) + oj.s, g.L - m2)) continue;
        const long long tE = std::max(a2 + m2 + g.d, g.r);
        const long long theta = tE + R - S - m1 - m2;
        if (tE > t_last || theta > cap) continue;
        consider(theta, BreakPattern::split(static_cast<int>(i), static_cast<int>(j), r.split_break_first,
                                            r.split_break_second));
      }
    }
  }
  if (!found) return out;

  out.legal = true;
  out.service_minutes = static_cast<Minutes>(best_theta);
  out.driving_minutes = static_cast<Minutes>(drive_total);
  out.km = km;
  out.cost = cost_of(out.service_minutes, km, true);
  out.breaks = best;
  return out;
}

}  // namespace legalvrp::heuristics
