#pragma once
// legalvrp::week — one day, end to end (brief §6.7, §7, §8; "the checker is the judge"):
//   1. territory baseline (current practice; always legal, checked);
//   2. daily MILP with the baseline as a complete MIP start;
//   3. extraction, then the independent checker;
//   4. if the incumbent fails the checker, re-time its sequences with the exact evaluator and
//      check again; if there is still a problem, or no incumbent at the time limit, the
//      baseline is returned and the day is flagged;
//   5. never worse than the baseline: when the legal MILP plan costs more than the baseline (checker's
//      recomputed objective; possible when the time limit stops the search, since the model's cost and
//      the checker's differ), its sequences are re-timed by the exact evaluator, and if that is still
//      more expensive the baseline is returned (source baseline_better, not flagged as a fallback).
// The returned plan always has zero checker violations.

#include <filesystem>
#include <optional>
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
  baseline_better,      // legal MILP plan (re-timed or not) more expensive than the baseline start:
                        // possible at the time limit, the model's and the checker's costs differ
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

// Same sequences as `plan`, schedules from the exact evaluator; nullopt if a sequence is illegal.
[[nodiscard]] std::optional<DayPlan> retime_plan(const DayInstance& day, const DayPlan& plan);

// Brief §11 per-day outputs in `dir`: plan.json (the validated plan), stats.json (§6.7 solver
// statistics + source, baseline and recomputed objectives, model size, cuts, start accepted),
// violations.json (the checker's list: must be empty). Pass the same `dir` as
// MilpOptions::log_dir with tag "gurobi" to get dir/gurobi.log.
void write_day_outputs(const std::filesystem::path& dir, const DaySolve& solve);

}  // namespace legalvrp::week
