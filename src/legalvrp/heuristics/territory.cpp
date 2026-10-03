#include "legalvrp/heuristics/territory.hpp"

#include <algorithm>
#include <limits>

#include "legalvrp/heuristics/assignment.hpp"
#include "legalvrp/heuristics/kmeans.hpp"
#include "legalvrp/heuristics/route_eval.hpp"

namespace legalvrp::heuristics {

namespace {

constexpr double kEps = 1e-9;
using Seq = std::vector<std::size_t>;

class Builder {
 public:
  explicit Builder(const DayInstance& day) : day_(day), eval_(day) {
    depot_ = day.matrix.index_of(day.depot.id);
    for (const auto& o : day.orders) node_.push_back(day.matrix.index_of(o.customer_id));
  }

  [[nodiscard]] RouteEvaluation eval(std::size_t k, const Seq& s) const { return eval_.evaluate(k, s); }
  [[nodiscard]] double penalty(std::size_t i) const { return day_.orders[i].postpone_penalty; }

  [[nodiscard]] bool equipment_ok(std::size_t k, std::size_t i) const {
    const Truck& t = day_.truck(day_.drivers[k].truck_id);
    const Order& o = day_.orders[i];
    return (!day_.customer(o.customer_id).needs_tail_lift || t.has_tail_lift) &&
           o.pallets <= t.capacity_pallets;
  }

  // Nearest neighbour by travel time from the depot.
  [[nodiscard]] Seq nearest_neighbour(Seq todo) const {
    Seq s;
    std::size_t here = depot_;
    while (!todo.empty()) {
      auto best = todo.begin();
      for (auto it = todo.begin(); it != todo.end(); ++it) {
        if (day_.matrix.time(here, node_[*it]) < day_.matrix.time(here, node_[*best])) best = it;
      }
      here = node_[*best];
      s.push_back(*best);
      todo.erase(best);
    }
    return s;
  }

  // Travel minutes saved by removing position p of s (its detour).
  [[nodiscard]] Minutes detour(const Seq& s, std::size_t p) const {
    const std::size_t prev = p == 0 ? depot_ : node_[s[p - 1]];
    const std::size_t next = p + 1 == s.size() ? depot_ : node_[s[p + 1]];
    const Matrix& m = day_.matrix;
    return m.time(prev, node_[s[p]]) + m.time(node_[s[p]], next) - m.time(prev, next);
  }

  // Drops orders until legal. Each step removes the order whose removal gives a legal route
  // of least (cost + dropped penalty); if no single removal is legal, the order with the
  // largest detour. Returns the dropped orders.
  Seq repair(std::size_t k, Seq& s) const {
    Seq dropped;
    while (!s.empty() && !eval(k, s).legal) {
      std::size_t pick = 0;
      bool pick_legal = false;
      double pick_score = std::numeric_limits<double>::max();
      for (std::size_t p = 0; p < s.size(); ++p) {
        Seq t = s;
        t.erase(t.begin() + static_cast<std::ptrdiff_t>(p));
        const auto e = eval(k, t);
        const double score = e.legal ? e.cost + penalty(s[p]) : -static_cast<double>(detour(s, p));
        if ((e.legal && !pick_legal) || (e.legal == pick_legal && score < pick_score)) {
          pick = p;
          pick_legal = e.legal;
          pick_score = score;
        }
      }
      dropped.push_back(s[pick]);
      s.erase(s.begin() + static_cast<std::ptrdiff_t>(pick));
    }
    return dropped;
  }

  // 2-opt (segment reversal), first improvement, legal moves only.
  void two_opt(std::size_t k, Seq& s) const {
    double cur = eval(k, s).cost;
    for (bool improved = true; improved;) {
      improved = false;
      for (std::size_t i = 0; i + 1 < s.size() && !improved; ++i) {
        for (std::size_t j = i + 1; j < s.size() && !improved; ++j) {
          Seq t = s;
          std::reverse(t.begin() + static_cast<std::ptrdiff_t>(i),
                       t.begin() + static_cast<std::ptrdiff_t>(j) + 1);
          const auto e = eval(k, t);
          if (e.legal && e.cost < cur - kEps) {
            s = std::move(t);
            cur = e.cost;
            improved = true;
          }
        }
      }
    }
  }

  // Route for driver k over `orders`: equipment filter, NN, repair, 2-opt.
  // Returns the route; `left` receives the orders it does not serve.
  Seq build(std::size_t k, const Seq& orders, Seq& left) const {
    Seq ok;
    for (const auto i : orders) (equipment_ok(k, i) ? ok : left).push_back(i);
    Seq s = nearest_neighbour(ok);
    const Seq dropped = repair(k, s);
    left.insert(left.end(), dropped.begin(), dropped.end());
    two_opt(k, s);
    return s;
  }

  [[nodiscard]] const DayInstance& day() const { return day_; }

 private:
  const DayInstance& day_;
  RouteEvaluator eval_;
  std::size_t depot_ = 0;
  std::vector<std::size_t> node_;
};

}  // namespace

BaselineSequences territory_sequences(const DayInstance& day) {
  const Builder b(day);
  const std::size_t K = day.drivers.size();
  const std::size_t n = day.orders.size();
  BaselineSequences out;
  out.routes.assign(K, {});
  if (n == 0) return out;
  if (K == 0) {
    for (std::size_t i = 0; i < n; ++i) out.postponed.push_back(i);
    return out;
  }

  // 1. territories
  std::vector<Point2> pts;
  for (const auto& o : day.orders) {
    const Customer& c = day.customer(o.customer_id);
    pts.push_back({c.x_km, c.y_km});
  }
  const KMeansResult km = kmeans(pts, K);
  const std::size_t T = km.centers.size();
  std::vector<Seq> territory(T);
  for (std::size_t i = 0; i < n; ++i) territory[km.label[i]].push_back(i);

  // 2. assignment on the cost of actually routing territory t with driver k
  std::vector<std::vector<double>> cost(T, std::vector<double>(K, 0.0));
  for (std::size_t t = 0; t < T; ++t) {
    for (std::size_t k = 0; k < K; ++k) {
      Seq left;
      const Seq s = b.build(k, territory[t], left);
      double c = b.eval(k, s).cost;
      for (const auto i : left) c += b.penalty(i);
      cost[t][k] = c;
    }
  }
  const auto assign = min_cost_assignment(cost);

  // 3. routes
  Seq pool;
  for (std::size_t t = 0; t < T; ++t) {
    if (!assign[t]) {
      pool.insert(pool.end(), territory[t].begin(), territory[t].end());
      continue;
    }
    out.routes[*assign[t]] = b.build(*assign[t], territory[t], pool);
  }

  // 4. leftovers: highest penalty first, cheapest legal insertion if cheaper than postponing
  std::ranges::sort(pool, [&](std::size_t a, std::size_t c) {
    if (b.penalty(a) != b.penalty(c)) return b.penalty(a) > b.penalty(c);
    return a < c;
  });
  std::vector<double> route_cost(K);
  for (std::size_t k = 0; k < K; ++k) route_cost[k] = b.eval(k, out.routes[k]).cost;
  for (const auto i : pool) {
    double best_delta = b.penalty(i) - kEps;
    std::size_t best_k = K, best_pos = 0;
    double best_cost = 0.0;
    for (std::size_t k = 0; k < K; ++k) {
      if (!b.equipment_ok(k, i)) continue;
      for (std::size_t pos = 0; pos <= out.routes[k].size(); ++pos) {
        Seq t = out.routes[k];
        t.insert(t.begin() + static_cast<std::ptrdiff_t>(pos), i);
        const auto e = b.eval(k, t);
        if (e.legal && e.cost - route_cost[k] < best_delta) {
          best_delta = e.cost - route_cost[k];
          best_k = k;
          best_pos = pos;
          best_cost = e.cost;
        }
      }
    }
    if (best_k < K) {
      out.routes[best_k].insert(out.routes[best_k].begin() + static_cast<std::ptrdiff_t>(best_pos), i);
      route_cost[best_k] = best_cost;
    } else {
      out.postponed.push_back(i);
    }
  }
  for (std::size_t k = 0; k < K; ++k) b.two_opt(k, out.routes[k]);
  std::ranges::sort(out.postponed);
  return out;
}

DayPlan territory_baseline(const DayInstance& day) {
  const BaselineSequences seq = territory_sequences(day);
  const RouteEvaluator ev(day);
  DayPlan plan;
  plan.day = day.day;
  for (std::size_t k = 0; k < day.drivers.size(); ++k) {
    const auto e = ev.evaluate(k, seq.routes[k]);  // legal by construction (empty is legal)
    plan.objective += e.cost;
    if (!seq.routes[k].empty()) plan.routes.push_back(e.route);
  }
  for (const auto i : seq.postponed) {
    plan.postponed_order_ids.push_back(day.orders[i].id);
    plan.objective += day.orders[i].postpone_penalty;
  }
  plan.solver_stats.status = "BASELINE";
  return plan;
}

}  // namespace legalvrp::heuristics
