#pragma once
// legalvrp::heuristics — territory baseline (PROJECT_BRIEF §7): current practice and MIP start.
//
//  1. Territories: k-means on the orders' customer coordinates, k = number of drivers.
//  2. Assignment: Hungarian on cost(territory t, driver k) = cost of the route the driver would
//     drive for t (steps 3) + penalties of the orders it would drop (D-026; the brief's "centroid
//     distance" is the same for every driver, who all start at the depot).
//  3. Routing per driver: nearest neighbour from the depot (travel time), then repair (drop the
//     order whose removal is best until the evaluator finds the route legal), then 2-opt keeping
//     only legal, cheaper moves.
//  4. Leftovers (dropped, incompatible, unassigned): cheapest legal insertion over every route
//     and position if it costs less than the order's postponement penalty; else postponed.
// Deterministic. Routes and times come from the exact RouteEvaluator, so the plan is legal by
// construction; the checker confirms it independently.

#include <cstddef>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::heuristics {

[[nodiscard]] DayPlan territory_baseline(const DayInstance& day);

// Exposed for tests: per-driver sequences (indices into day.orders) and the postponed indices.
struct BaselineSequences {
  std::vector<std::vector<std::size_t>> routes;  // one per driver, same order as day.drivers
  std::vector<std::size_t> postponed;
};
[[nodiscard]] BaselineSequences territory_sequences(const DayInstance& day);

}  // namespace legalvrp::heuristics
