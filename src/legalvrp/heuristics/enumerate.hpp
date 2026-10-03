#pragma once
// legalvrp::heuristics — exact day solver by enumeration (T6 ground truth, small days only).
//
// Enumerates every assignment of orders to drivers or postponement, and every visiting order
// of each driver's set; each sequence is timed by the exact RouteEvaluator (best break position
// and schedule, D-016). best[k][S] = cheapest legal sequence of set S for driver k; the optimum
// combines disjoint sets over drivers, the rest postponed. Exact for the model of docs/MODEL.md.
// Cost: sum over k and S of |S|! evaluations — use for n <= 7.

#include "legalvrp/domain/models.hpp"

namespace legalvrp::heuristics {

struct ExactDay {
  DayPlan plan;       // an optimal plan (times from the evaluator)
  Euros objective = 0.0;
  long long sequences_evaluated = 0;
};

// Throws std::invalid_argument if the day has more than 8 orders.
[[nodiscard]] ExactDay solve_by_enumeration(const DayInstance& day);

}  // namespace legalvrp::heuristics
