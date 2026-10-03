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
#include <string>
#include <utility>
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

// A window of days [first_day, last_day] of the week (V1-T5 look-ahead): starts from the weekly state
// `states` and the orders `carried` from earlier days (original orders, postponed_from set).
// Penalties use the real day numbers (escalation; end-of-week penalty only on the week's last
// day); weekly caps and overtime include the state. last_day < 0 means the week's last day.
struct WindowSpec {
  int first_day = 0;
  int last_day = -1;
  std::vector<DriverWeekState> states;
  std::vector<Order> carried;
  // V1-T5 (ii), soft budget: weekly service minutes kept for days after the window, per driver.
  // Using them costs reserve_cost EUR/min:  r_k >= W0_k + svc_k - (H^max_k - reserve_k).
  std::vector<std::pair<std::string, double>> reserve;
  double reserve_cost = 0.0;
};

// `start`: one feasible plan per window day, used as a MIP start. No connectivity cuts.
[[nodiscard]] ClairvoyantResult solve_window(GRBEnv& env, const WeekInstance& week, const WindowSpec& window,
                                             const MilpOptions& options,
                                             const std::vector<DayPlan>* start = nullptr);

// The whole week from a zero state (T11): solve_window with the default spec.
// `start`: a feasible week (e.g. the rolling plans), used as a MIP start. No connectivity cuts.
[[nodiscard]] ClairvoyantResult solve_week_clairvoyant(GRBEnv& env, const WeekInstance& week,
                                                       const MilpOptions& options,
                                                       const std::vector<DayPlan>* start = nullptr);

}  // namespace legalvrp::model
