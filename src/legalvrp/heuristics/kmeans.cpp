#include "legalvrp/heuristics/kmeans.hpp"

#include <algorithm>
#include <limits>

namespace legalvrp::heuristics {

namespace {
double dist2(const Point2& a, const Point2& b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  return dx * dx + dy * dy;
}
}  // namespace

KMeansResult kmeans(const std::vector<Point2>& points, std::size_t k, int max_iterations) {
  KMeansResult out;
  const std::size_t n = points.size();
  if (n == 0) return out;
  k = std::clamp<std::size_t>(k, 1, n);

  // Farthest-first initialisation.
  std::vector<double> nearest(n, std::numeric_limits<double>::max());
  std::size_t first = 0;
  for (std::size_t i = 1; i < n; ++i) {
    if (dist2(points[i], {}) > dist2(points[first], {})) first = i;
  }
  out.centers.push_back(points[first]);
  while (out.centers.size() < k) {
    std::size_t best = 0;
    for (std::size_t i = 0; i < n; ++i) {
      nearest[i] = std::min(nearest[i], dist2(points[i], out.centers.back()));
      if (nearest[i] > nearest[best]) best = i;
    }
    out.centers.push_back(points[best]);
  }

  // Lloyd iterations.
  out.label.assign(n, 0);
  for (int it = 0; it < max_iterations; ++it) {
    bool changed = (it == 0);
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t best = 0;
      for (std::size_t c = 1; c < k; ++c) {
        if (dist2(points[i], out.centers[c]) < dist2(points[i], out.centers[best])) best = c;
      }
      if (best != out.label[i]) changed = true;
      out.label[i] = best;
    }
    if (!changed) break;
    std::vector<Point2> sum(k);
    std::vector<std::size_t> count(k, 0);
    for (std::size_t i = 0; i < n; ++i) {
      sum[out.label[i]].x += points[i].x;
      sum[out.label[i]].y += points[i].y;
      ++count[out.label[i]];
    }
    for (std::size_t c = 0; c < k; ++c) {
      if (count[c] > 0) {  // an empty cluster keeps its previous center
        out.centers[c] = {sum[c].x / static_cast<double>(count[c]),
                          sum[c].y / static_cast<double>(count[c])};
      }
    }
  }
  return out;
}

}  // namespace legalvrp::heuristics
