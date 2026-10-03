#include "legalvrp/heuristics/assignment.hpp"

#include <limits>
#include <stdexcept>

namespace legalvrp::heuristics {

std::vector<std::optional<std::size_t>> min_cost_assignment(
    const std::vector<std::vector<double>>& cost) {
  const std::size_t n = cost.size();  // rows
  if (n == 0) return {};
  const std::size_t m = cost[0].size();  // columns
  if (m < n) throw std::invalid_argument("min_cost_assignment: need rows <= columns");

  // Kuhn-Munkres with potentials (1-indexed; column 0 is a sentinel).
  constexpr double kInf = std::numeric_limits<double>::infinity();
  std::vector<double> u(n + 1, 0.0), v(m + 1, 0.0);
  std::vector<std::size_t> p(m + 1, 0), way(m + 1, 0);  // p[j]: row matched to column j
  for (std::size_t i = 1; i <= n; ++i) {
    p[0] = i;
    std::size_t j0 = 0;
    std::vector<double> minv(m + 1, kInf);
    std::vector<bool> used(m + 1, false);
    do {
      used[j0] = true;
      const std::size_t i0 = p[j0];
      double delta = kInf;
      std::size_t j1 = 0;
      for (std::size_t j = 1; j <= m; ++j) {
        if (used[j]) continue;
        const double cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
        if (cur < minv[j]) {
          minv[j] = cur;
          way[j] = j0;
        }
        if (minv[j] < delta) {
          delta = minv[j];
          j1 = j;
        }
      }
      for (std::size_t j = 0; j <= m; ++j) {
        if (used[j]) {
          u[p[j]] += delta;
          v[j] -= delta;
        } else {
          minv[j] -= delta;
        }
      }
      j0 = j1;
    } while (p[j0] != 0);
    do {
      const std::size_t j1 = way[j0];
      p[j0] = p[j1];
      j0 = j1;
    } while (j0 != 0);
  }

  std::vector<std::optional<std::size_t>> row_to_col(n);
  for (std::size_t j = 1; j <= m; ++j) {
    if (p[j] != 0 && cost[p[j] - 1][j - 1] < kForbidden) row_to_col[p[j] - 1] = j - 1;
  }
  return row_to_col;
}

}  // namespace legalvrp::heuristics
