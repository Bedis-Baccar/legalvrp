#include "legalvrp/model/bigm.hpp"

#include <algorithm>
#include <limits>

namespace legalvrp::model {

const char* to_string(Formulation f) noexcept {
  return f == Formulation::strong ? "strong" : "reference";
}

namespace {

struct Ctx {
  const DayInstance& day;
  Prep& p;
  [[nodiscard]] std::size_t mnode(int v) const {
    return (v == p.depot_out || v == p.depot_in) ? p.depot_node : p.node[static_cast<std::size_t>(v)];
  }
  [[nodiscard]] Minutes t(int a, int b) const {
    if (a == p.depot_out && b == p.depot_in) return 0;
    return day.matrix.time(mnode(a), mnode(b));
  }
  [[nodiscard]] double d(int a, int b) const {
    if (a == p.depot_out && b == p.depot_in) return 0.0;
    return day.matrix.dist(mnode(a), mnode(b));
  }
};

}  // namespace

Prep prepare(const DayInstance& day, Formulation f) {
  Prep p;
  p.formulation = f;
  p.n = static_cast<int>(day.orders.size());
  p.K = static_cast<int>(day.drivers.size());
  p.depot_out = p.n;
  p.depot_in = p.n + 1;
  p.depot_node = day.matrix.index_of(day.depot.id);
  const auto n = static_cast<std::size_t>(p.n);
  const auto K = static_cast<std::size_t>(p.K);
  const Rules& r = day.rules;
  const Ctx c{day, p};

  for (const auto& o : day.orders) {
    const Customer& cu = day.customer(o.customer_id);
    p.node.push_back(day.matrix.index_of(cu.id));
    p.e.push_back(cu.window_start);
    p.l.push_back(cu.window_end);
    p.s.push_back(o.service_mu);
    p.q.push_back(o.pallets);
    p.penalty.push_back(o.postpone_penalty);
  }

  // Travel and distance over model nodes (orders, depot_out, depot_in); depot_out -> depot_in is the empty route.
  const std::size_t N = n + 2;
  p.tt.assign(N * N, 0);
  p.dd.assign(N * N, 0.0);
  for (std::size_t a = 0; a < N; ++a) {
    for (std::size_t b = 0; b < N; ++b) {
      p.tt[a * N + b] = c.t(static_cast<int>(a), static_cast<int>(b));
      p.dd[a * N + b] = c.d(static_cast<int>(a), static_cast<int>(b));
    }
  }

  // Shortest travel times among the day's nodes (orders + depot at index n), Floyd-Warshall.
  p.sp.assign(n + 1, std::vector<Minutes>(n + 1, 0));
  for (std::size_t a = 0; a <= n; ++a) {
    for (std::size_t b = 0; b <= n; ++b) {
      const int va = a == n ? p.depot_out : static_cast<int>(a);
      const int vb = b == n ? p.depot_in : static_cast<int>(b);
      p.sp[a][b] = (a == b) ? 0 : c.t(va, vb);
    }
  }
  for (std::size_t m = 0; m <= n; ++m) {
    for (std::size_t a = 0; a <= n; ++a) {
      for (std::size_t b = 0; b <= n; ++b) {
        p.sp[a][b] = std::min(p.sp[a][b], p.sp[a][m] + p.sp[m][b]);
      }
    }
  }

  // Equipment and load compatibility (C_k, ??4).
  p.compat.assign(K, std::vector<bool>(n, false));
  std::vector<const Truck*> truck;
  for (std::size_t k = 0; k < K; ++k) {
    truck.push_back(&day.truck(day.drivers[k].truck_id));
    for (std::size_t i = 0; i < n; ++i) {
      const bool lift = !day.customer(day.orders[i].customer_id).needs_tail_lift || truck[k]->has_tail_lift;
      p.compat[k][i] = lift && p.q[i] <= truck[k]->capacity_pallets;
    }
  }
  p.servable.assign(n, false);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t k = 0; k < K; ++k) p.servable[i] = p.servable[i] || p.compat[k][i];
  }
  p.arcs_full = p.K * (p.n * (p.n - 1) + 2 * p.n + 1);

  const auto S = [&](std::size_t k) { return day.drivers[k].shift_start; };
  const auto F = [&](std::size_t k) { return day.drivers[k].shift_end_max; };
  const Minutes P = r.depot_prep, R = r.depot_close;

  if (f == Formulation::strong) {
    const std::vector<Minutes> e0 = p.e, l0 = p.l;
    auto unservable = [&](std::size_t i) {
      p.servable[i] = false;
      for (std::size_t k = 0; k < K; ++k) p.compat[k][i] = false;
    };
    // A route through i drives at least sp(0,i) + sp(i,0); more than DD is never legal.
    for (std::size_t i = 0; i < n; ++i) {
      if (p.sp[n][i] + p.sp[i][n] > r.daily_drive_max) unservable(i);
    }
    // Pair arc allowed for some driver: legs > DB are impossible (breaks only at customers).
    auto pair_ok = [&](std::size_t i, std::size_t j) {
      if (i == j || !p.servable[i] || !p.servable[j]) return false;
      const Minutes tij = c.t(static_cast<int>(i), static_cast<int>(j));
      if (tij > r.drive_before_break || p.e[i] + p.s[i] + tij > p.l[j]) return false;
      for (std::size_t k = 0; k < K; ++k) {
        if (p.compat[k][i] && p.compat[k][j] && p.q[i] + p.q[j] <= truck[k]->capacity_pallets) return true;
      }
      return false;
    };
    // Time-window reduction to a fixpoint (Desrochers et al. 1992, predecessor/successor rules).
    for (bool changed = true; changed;) {
      changed = false;
      for (std::size_t j = 0; j < n; ++j) {
        if (!p.servable[j]) continue;
        Minutes earliest = std::numeric_limits<Minutes>::max();
        Minutes latest = std::numeric_limits<Minutes>::min();
        const Minutes t0j = c.t(p.depot_out, static_cast<int>(j));
        const Minutes tj0 = c.t(static_cast<int>(j), p.depot_in);
        for (std::size_t k = 0; k < K; ++k) {
          if (!p.compat[k][j]) continue;
          if (t0j <= r.drive_before_break) earliest = std::min(earliest, S(k) + P + t0j);
          if (tj0 <= r.drive_before_break) latest = std::max(latest, F(k) - R - tj0 - p.s[j]);
        }
        for (std::size_t i = 0; i < n; ++i) {
          if (pair_ok(i, j)) earliest = std::min(earliest, p.e[i] + p.s[i] + c.t(static_cast<int>(i), static_cast<int>(j)));
          if (pair_ok(j, i)) latest = std::max(latest, p.l[i] - p.s[j] - c.t(static_cast<int>(j), static_cast<int>(i)));
        }
        if (earliest == std::numeric_limits<Minutes>::max() || latest == std::numeric_limits<Minutes>::min()) {
          unservable(j);  // no predecessor or no successor
          changed = true;
          continue;
        }
        if (earliest > p.e[j]) { p.e[j] = earliest; changed = true; }
        if (latest < p.l[j]) { p.l[j] = latest; changed = true; }
        if (p.e[j] > p.l[j]) { unservable(j); changed = true; }
      }
    }
    // Per-driver reachability: depot -> i -> depot within the driver's shift.
    for (std::size_t k = 0; k < K; ++k) {
      for (std::size_t i = 0; i < n; ++i) {
        if (!p.compat[k][i]) continue;
        const Minutes start = std::max(p.e[i], S(k) + P + p.sp[n][i]);
        if (start > p.l[i] || start + p.s[i] + p.sp[i][n] + R > F(k)) p.compat[k][i] = false;
      }
    }
    for (std::size_t i = 0; i < n; ++i) {
      bool any = false;
      for (std::size_t k = 0; k < K; ++k) any = any || p.compat[k][i];
      if (!any) unservable(i);
      if (p.servable[i]) p.window_minutes_removed += (l0[i] - e0[i]) - (p.l[i] - p.e[i]);
    }
    // Driving on arrival: at least the shortest path from the depot, at most DD minus the way back.
    p.D_lb.assign(n, 0);
    p.D_ub.assign(n, r.daily_drive_max);
    for (std::size_t i = 0; i < n; ++i) {
      if (!p.servable[i]) continue;
      p.D_lb[i] = p.sp[n][i];
      p.D_ub[i] = r.daily_drive_max - p.sp[i][n];
    }
    // Forced breaks: serving i alone already exceeds 4 h 30 of driving or 6 h of work.
    p.force_break.assign(K, std::vector<bool>(n, false));
    for (std::size_t k = 0; k < K; ++k) {
      for (std::size_t i = 0; i < n; ++i) {
        if (!p.compat[k][i]) continue;
        const bool drive = p.sp[n][i] + p.sp[i][n] > r.drive_before_break;
        const Minutes start = std::max(p.e[i], S(k) + P + p.sp[n][i]);
        const bool work = start + p.s[i] + p.sp[i][n] + R - S(k) > r.work_before_break;
        p.force_break[k][i] = drive || work;
      }
    }
  } else {
    p.D_lb.assign(n, 0);
    p.D_ub.assign(n, r.daily_drive_max);
    p.force_break.assign(K, std::vector<bool>(n, false));
  }

  // Arcs per driver.
  p.arcs.assign(K, {});
  for (std::size_t k = 0; k < K; ++k) {
    auto add = [&](int a, int b) { p.arcs[k].push_back({a, b, c.t(a, b), c.d(a, b)}); };
    add(p.depot_out, p.depot_in);  // empty route
    for (std::size_t j = 0; j < n; ++j) {
      if (!p.compat[k][j]) continue;
      const int vj = static_cast<int>(j);
      if (f == Formulation::reference) {
        add(p.depot_out, vj);
        add(vj, p.depot_in);
        for (std::size_t i = 0; i < n; ++i) {
          if (i == j || !p.compat[k][i]) continue;
          const int vi = static_cast<int>(i);
          if (p.e[i] + p.s[i] + c.t(vi, vj) <= p.l[j]) add(vi, vj);  // brief ??6.1 pruning
        }
        continue;
      }
      const Minutes t0j = c.t(p.depot_out, vj);
      if (t0j <= r.drive_before_break && S(k) + P + t0j <= p.l[j]) add(p.depot_out, vj);
      const Minutes tj0 = c.t(vj, p.depot_in);
      if (tj0 <= r.drive_before_break && p.e[j] + p.s[j] + tj0 + R <= F(k)) add(vj, p.depot_in);
      for (std::size_t i = 0; i < n; ++i) {
        if (i == j || !p.compat[k][i]) continue;
        const int vi = static_cast<int>(i);
        const Minutes tij = c.t(vi, vj);
        if (tij > r.drive_before_break || p.e[i] + p.s[i] + tij > p.l[j]) continue;
        if (p.q[i] + p.q[j] > truck[k]->capacity_pallets) continue;
        add(vi, vj);
      }
    }
    p.arcs_total += static_cast<int>(p.arcs[k].size());
  }
  return p;
}

}  // namespace legalvrp::model
