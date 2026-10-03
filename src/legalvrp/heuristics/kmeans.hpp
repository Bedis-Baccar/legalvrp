#pragma once
// legalvrp::heuristics — k-means on 2D points (Lloyd), deterministic.
// Initialisation: farthest-first (maximin) starting from the point farthest from the origin
// (the depot); ties go to the lowest index. No randomness, so the baseline is reproducible
// on every platform without an RNG.

#include <cstddef>
#include <vector>

namespace legalvrp::heuristics {

struct Point2 {
  double x = 0.0;
  double y = 0.0;
};

struct KMeansResult {
  std::vector<std::size_t> label;  // cluster of each point, in [0, k)
  std::vector<Point2> centers;
};

// k is clamped to [1, points.size()]; empty input gives an empty result.
[[nodiscard]] KMeansResult kmeans(const std::vector<Point2>& points, std::size_t k,
                                  int max_iterations = 100);

}  // namespace legalvrp::heuristics
