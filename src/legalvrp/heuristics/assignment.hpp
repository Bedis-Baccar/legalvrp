#pragma once
// legalvrp::heuristics — minimum-cost assignment (Hungarian algorithm, O(n^2 m)).
// Rectangular: rows <= columns; every row gets a distinct column. Entries >= kForbidden
// mark forbidden pairs; a row whose only options are forbidden is left unassigned.

#include <cstddef>
#include <optional>
#include <vector>

namespace legalvrp::heuristics {

inline constexpr double kForbidden = 1e15;

// cost[r][c]; returns for each row its column, or nullopt if only forbidden pairs remain.
[[nodiscard]] std::vector<std::optional<std::size_t>> min_cost_assignment(
    const std::vector<std::vector<double>>& cost);

}  // namespace legalvrp::heuristics
