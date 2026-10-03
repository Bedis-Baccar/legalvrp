#pragma once
// legalvrp::kpi — week KPIs (PROJECT_BRIEF §11), computed only from the checker's
// recomputation of the extracted routes, never from model variables.
//
//   cost_total      true weekly cost: km + regular time + fixed + overtime on the FINAL weekly
//                   hours + postponement penalties. (Summing the daily objectives would count
//                   overtime several times: each day's objective includes the week's excess so far.)
//   km              total distance
//   drivers_used    per day
//   service_hours   per driver per day and per week
//   extra_hours     weekly minutes above the contract threshold, per driver
//   postponed       orders postponed per day; orders unserved at the end of the week
//   on_time_rate    served orders whose service started inside the window (1.0 in V0)
//   hours_gini      Gini coefficient of weekly service hours across full-time drivers
//   solver          status, runtime, gap, nodes per day (from DayPlan::solver_stats)

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::kpi {

struct DayKpis {
  int day = 0;
  int orders = 0;                    // incl. carried
  int served = 0;
  int postponed = 0;
  int drivers_used = 0;
  double km = 0.0;
  Euros cost = 0.0;                  // recomputed objective of the day
  Euros postponement_cost = 0.0;
  std::map<std::string, Minutes> service_minutes;  // per driver (0 if idle)
  SolverStats solver;
};

struct DriverWeek {
  std::string driver_id;
  std::string contract;
  Minutes service_minutes = 0;
  Minutes driving_minutes = 0;
  Minutes threshold = 0;
  Minutes extra_minutes = 0;         // max(0, service - threshold)
  int days_worked = 0;
};

struct WeekKpis {
  std::string instance;
  std::uint64_t seed = 0;
  std::vector<DayKpis> days;
  std::vector<DriverWeek> drivers;
  Euros cost_total = 0.0;            // TRUE weekly cost: routes + overtime on final weekly hours + penalties
  Euros sum_daily_objectives = 0.0;  // sum of the days' objectives (counts overtime cumulatively, D-042)
  double km = 0.0;
  int served = 0;
  int postponement_decisions = 0;
  int unserved_end = 0;
  std::vector<std::string> unserved_order_ids;
  double on_time_rate = 1.0;
  double hours_gini = 0.0;           // full-time drivers
  Minutes extra_minutes_total = 0;
  bool week_checker_ok = false;
  int violations = 0;
};

// Gini coefficient: sum_i sum_j |x_i - x_j| / (2 n^2 mean); 0 for empty, all-zero or equal input.
[[nodiscard]] double gini(const std::vector<double>& x);

// `full_time_contract`: the contract class whose drivers enter hours_gini.
[[nodiscard]] WeekKpis compute_week_kpis(const WeekInstance& week, const std::vector<DayInstance>& days,
                                         const std::vector<DayPlan>& plans, const check::WeekCheck& week_check,
                                         const std::string& full_time_contract = "full_time");

}  // namespace legalvrp::kpi
