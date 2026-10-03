#pragma once
// legalvrp::model — clairvoyant weekly MILP (task T11): the whole week in one model, with
// every order known in advance. Measures the "price of myopia" of the rolling loop.
//
// Day d is a daily model (MilpModel weekly part) over every order released on or before d.
// Linking, for each order i released on day r_i:
//   served once:      sum_{d >= r_i} z[i,d] <= 1,   z[i,d] = sum_k visit[i,k,d]
//   postponement:     the rolling accounting exactly (D-019) — on each day t >= r_i on which i
//                     is not yet served, pay p_i(t) = base * esc^(t - r_i) (>= end penalty on the
//                     last day):  sum_t p_i(t) * (1 - sum_{d = r_i}^{t} z[i,d])
//   weekly caps:      sum_d svc[k,d] <= H^max_k;  sum_d drive[k,d] <= WD
//   overtime:         o_k >= sum_d svc[k,d] - H^thr_k, cost c^ext_k * o_k (on the weekly total)
// Daily rest holds by construction (fixed start, D-018; duty window D-020).
// The rolling plans are a feasible start, so the clairvoyant cost is never above the rolling
// one (true weekly cost, kpi::WeekKpis::cost_total).
//
// Size: about the days' models added up, with orders accumulating; use reduced instances.

#include <optional>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/model/solve.hpp"

namespace legalvrp::model {

struct ClairvoyantResult {
  std::vector<DayPlan> plans;  // one per day; postponed = released, not yet served
  SolverStats stats;
  bool start_accepted = false;
};

// `start`: a feasible week (e.g. the rolling plans), used as a MIP start. No connectivity cuts.
[[nodiscard]] ClairvoyantResult solve_week_clairvoyant(GRBEnv& env, const WeekInstance& week,
                                                       const MilpOptions& options,
                                                       const std::vector<DayPlan>* start = nullptr);

}  // namespace legalvrp::model
