#pragma once
// legalvrp::week — week policies of V1-T5 for the rolling loop (Gurobi).
//
// (i) Look-ahead. Tomorrow's orders are known when today is planned (Q2). Day d solves the
//     window [d, d+h-1] as one MILP (model::solve_window: today's weekly state and carried
//     orders, the real days' escalating penalties, weekly caps and overtime over the window)
//     and commits day d only; the next day is solved again with what really happened.
// (ii) Scarce-skill protection. A driver who alone can serve some orders (tail-lift, pallets)
//     keeps part of his weekly capacity for the days after the window: a soft budget
//     (model::WindowSpec::reserve) sized from the exclusive orders seen so far.
//
// MIP start of the window: the myopic day (solve_day) followed by the territory baseline of the
// later days, built on that plan's carry-over and state. Today's plan then goes through the
// checker (and the exact re-timing); if anything fails, the myopic plan is committed.
// h = 1 without a reserve is the myopic loop (with the window model instead of the daily one).

#include <string>
#include <utility>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/loop.hpp"

namespace legalvrp::week {

struct LookaheadOptions {
  int horizon = 2;                 // days in the window, today included
  model::MilpOptions day;          // the myopic solve: MIP start and fallback
  model::MilpOptions window;       // the window MILP
  double reserve_cost = 0.0;       // (ii) EUR per reserved minute used; 0 = no protection
};

// myopic: no window needed (last day, no reserve); fallback: the window failed (myopic plan kept).
enum class LookaheadSource { window, window_retimed, myopic, fallback };
[[nodiscard]] const char* to_string(LookaheadSource s) noexcept;

struct LookaheadDay {
  DayPlan plan;                    // checked (zero violations)
  LookaheadSource source = LookaheadSource::window;
  double myopic_objective = 0.0;   // today's recomputed objective of the myopic plan
  SolverStats window_stats;
  std::vector<std::pair<std::string, double>> reserve;  // (ii) minutes kept per driver
};

[[nodiscard]] LookaheadDay solve_day_lookahead(GRBEnv& env, const WeekInstance& week, const DayInstance& day,
                                               const DayContext& context, const LookaheadOptions& options);

// (ii) Weekly service minutes to keep, per driver, for the orders only that driver can serve
// on the days after `last_known_day`: exclusive orders per known day (days 0..last_known_day)
// x mean single-order duty (depot round trip + service) x days left after last_known_day.
[[nodiscard]] std::vector<std::pair<std::string, double>> scarce_reserve(const WeekInstance& week,
                                                                         int last_known_day);

}  // namespace legalvrp::week
