#pragma once
// legalvrp::check — independent legality checker (PROJECT_BRIEF §8).
//
// Written from the rules in words (brief §3, docs/MODEL.md amendments D-007, D-018..D-020),
// NOT from heuristics/route_eval. Depends on legalvrp::domain only (enforced by CMake and
// tests/unit/test_layering.cpp). The checker is the judge: no plan is reported unless it
// returns zero violations.
//
// Day mode takes the plan as the driver will execute it: the reported departure, sequence,
// break position and service starts. It recomputes every arrival and the return from travel
// and service durations, and checks:
//   coverage        each order of the day served exactly once or postponed; known ids
//   consistency     one route per known driver; array sizes; arrivals and return equal to
//                   recomputed values; service never starts before arrival; break node on route
//   tail_lift, capacity
//   time_window     service start within [window_start, window_end]
//   shift           departure >= shift_start + prep; return + close <= shift_end_max;
//                   duty inside [earliest_duty_start, latest_duty_end] (D-020)
//   drive_*         before / after the break <= 270, or <= 270 without break; daily <= 540
//   work_*          fixed duty start S (D-018): break start - S <= 360,
//                   (return + close) - break end <= 360, or (return + close) - S <= 360
//   daily_service_max, weekly_service_max, weekly_drive_max   against the driver's state
// It also recomputes, per driver, temps de service, driving, km and the §6.3 cost, so KPIs
// and the next day's state never come from model variables (brief §11).
//
// Week mode replays the days: builds each DayInstance from the week (carrying postponed
// orders with escalating penalties, D-019), checks it, updates the drivers' weekly state from
// the recomputed facts, and checks the daily rest between consecutive duties.

#include <string>
#include <vector>

#include "legalvrp/domain/models.hpp"
#include "legalvrp/domain/violation.hpp"

namespace legalvrp::check {

struct DriverFacts {
  std::string driver_id;
  bool used = false;                 // served at least one order
  Minutes service_minutes = 0;       // temps de service (0 if unused)
  Minutes driving_minutes = 0;
  double km = 0.0;
  Minutes duty_start = 0;            // shift start (fixed, D-018), if used
  Minutes duty_end = 0;              // return + close, if used
  Euros cost = 0.0;                  // §6.3: km + regular + overtime + fixed
};

struct DayCheck {
  std::vector<Violation> violations;
  std::vector<DriverFacts> drivers;  // one per driver of the day, same order as day.drivers
  Euros postponement_cost = 0.0;
  Euros objective = 0.0;             // recomputed from the routes
  [[nodiscard]] bool ok() const noexcept { return violations.empty(); }
};

[[nodiscard]] DayCheck check_day(const DayInstance& day, const DayPlan& plan);

struct WeekCheck {
  std::vector<Violation> violations;          // day field set
  std::vector<DayCheck> days;
  std::vector<DriverWeekState> final_states;  // after the last day
  std::vector<std::string> unserved_order_ids;  // still postponed after the last day
  [[nodiscard]] bool ok() const noexcept { return violations.empty(); }
};

// plans[d] must be the plan of day d, for d = 0 .. week.days - 1.
[[nodiscard]] WeekCheck check_week(const WeekInstance& week, const std::vector<DayPlan>& plans);

}  // namespace legalvrp::check
