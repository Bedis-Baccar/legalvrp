#pragma once
// legalvrp::week — one day, end to end (brief §6.7, §7, §8; "the checker is the judge"):
//   1. territory baseline (current practice; always legal, checked);
//   2. daily MILP with the baseline as a complete MIP start;
//   3. extraction, then the independent checker;
//   4. if the incumbent fails the checker, re-time its sequences with the exact evaluator and
//      check again; if there is still a problem, or no incumbent at the time limit, the
//      baseline is returned and the day is flagged.
// The returned plan always has zero checker violations.

#include <string>

#include "gurobi_c++.h"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/model/solve.hpp"

namespace legalvrp::week {

enum class PlanSource {
  milp,                 // the MILP incumbent, as extracted
  milp_retimed,         // the MILP sequences re-timed by the evaluator
  baseline_no_incumbent,
  baseline_rejected,    // MILP plan failed the checker even after re-timing (a model bug)
};
[[nodiscard]] const char* to_string(PlanSource s) noexcept;

struct DaySolve {
  DayPlan plan;                    // validated
  check::DayCheck check;           // of `plan` (ok)
  DayPlan baseline;
  check::DayCheck baseline_check;
  model::MilpResult milp;          // raw solver result and statistics
  PlanSource source = PlanSource::milp;
};

[[nodiscard]] DaySolve solve_day(GRBEnv& env, const DayInstance& day, model::MilpOptions options);

}  // namespace legalvrp::week
