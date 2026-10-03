#include "legalvrp/heuristics/enumerate.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

#include "legalvrp/heuristics/route_eval.hpp"

namespace legalvrp::heuristics {

ExactDay solve_by_enumeration(const DayInstance& day) {
  const std::size_t n = day.orders.size();
  const std::size_t K = day.drivers.size();
  if (n > 8) throw std::invalid_argument("solve_by_enumeration: at most 8 orders");
  const RouteEvaluator ev(day);
  constexpr double kInf = std::numeric_limits<double>::infinity();
  const std::size_t sets = std::size_t{1} << n;

  ExactDay out;
  // best[k][S]: cheapest legal sequence of exactly the orders in S for driver k.
  std::vector<std::vector<double>> best(K, std::vector<double>(sets, kInf));
  std::vector<std::vector<std::vector<std::size_t>>> best_seq(K, std::vector<std::vector<std::size_t>>(sets));
  for (std::size_t k = 0; k < K; ++k) {
    for (std::size_t S = 0; S < sets; ++S) {
      std::vector<std::size_t> perm;
      for (std::size_t i = 0; i < n; ++i) {
        if (S & (std::size_t{1} << i)) perm.push_back(i);
      }
      do {
        ++out.sequences_evaluated;
        const auto e = ev.evaluate(k, perm);
        if (e.legal && e.cost < best[k][S]) {
          best[k][S] = e.cost;
          best_seq[k][S] = perm;
        }
      } while (std::next_permutation(perm.begin(), perm.end()));
    }
  }

  // f[k][U]: cheapest way for drivers k..K-1 to serve sets disjoint from U (U = already used).
  // Iterate over drivers with an explicit DP over "orders already assigned".
  std::vector<double> cur(sets, kInf);  // cost after assigning drivers 0..k-1, by union
  std::vector<std::vector<std::size_t>> choice(K, std::vector<std::size_t>(sets, 0));
  std::vector<std::vector<std::size_t>> parent(K, std::vector<std::size_t>(sets, 0));
  cur[0] = 0.0;
  for (std::size_t k = 0; k < K; ++k) {
    std::vector<double> next(sets, kInf);
    for (std::size_t U = 0; U < sets; ++U) {
      if (cur[U] == kInf) continue;
      const std::size_t free = (sets - 1) & ~U;
      for (std::size_t S = free;; S = (S - 1) & free) {  // all subsets of free, including 0
        if (best[k][S] < kInf && cur[U] + best[k][S] < next[U | S]) {
          next[U | S] = cur[U] + best[k][S];
          choice[k][U | S] = S;
          parent[k][U | S] = U;
        }
        if (S == 0) break;
      }
    }
    cur = std::move(next);
  }
  double best_total = kInf;
  std::size_t best_U = 0;
  for (std::size_t U = 0; U < sets; ++U) {
    if (cur[U] == kInf) continue;
    double total = cur[U];
    for (std::size_t i = 0; i < n; ++i) {
      if (!(U & (std::size_t{1} << i))) total += day.orders[i].postpone_penalty;
    }
    if (total < best_total) {
      best_total = total;
      best_U = U;
    }
  }

  // Rebuild the plan.
  out.objective = best_total;
  out.plan.day = day.day;
  std::size_t U = best_U;
  std::vector<std::size_t> set_of(K, 0);
  for (std::size_t k = K; k-- > 0;) {
    set_of[k] = choice[k][U];
    U = parent[k][U];
  }
  for (std::size_t k = 0; k < K; ++k) {
    if (set_of[k] == 0) continue;
    out.plan.routes.push_back(ev.evaluate(k, best_seq[k][set_of[k]]).route);
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (!(best_U & (std::size_t{1} << i))) out.plan.postponed_order_ids.push_back(day.orders[i].id);
  }
  out.plan.objective = best_total;
  out.plan.solver_stats.status = "ENUMERATION";
  return out;
}

}  // namespace legalvrp::heuristics
