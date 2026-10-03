// T8 acceptance (Gurobi): five days run end to end with the MILP; the week-mode checker
// returns zero violations; weekly state and carry-over are consistent; KPIs computed.
#include <catch2/catch_test_macros.hpp>

#include "gurobi_c++.h"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/week/loop.hpp"
#include "legalvrp/week/solve_day.hpp"

using namespace legalvrp;

TEST_CASE("five MILP days end to end: zero week-mode violations", "[week][acceptance]") {
  GRBEnv env(true);
  env.set(GRB_IntParam_OutputFlag, 0);
  env.start();
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  model::MilpOptions o;
  o.solver.time_limit = 8;  // short: the test checks the loop, not optimality
  int milp_days = 0;
  const auto run = week::run_week(w, [&](const DayInstance& day) {
    const auto s = week::solve_day(env, day, o);
    milp_days += s.source == week::PlanSource::milp ? 1 : 0;
    CHECK(s.plan.objective <= s.baseline_check.objective + 1e-6);
    return s.plan;
  });
  REQUIRE(run.plans.size() == 5);
  CHECK(run.week_check.ok());
  CHECK(milp_days == 5);
  for (const auto& dc : run.day_checks) CHECK(dc.ok());

  // Carry-over: what was postponed on day d is an order of day d + 1.
  for (std::size_t d = 0; d + 1 < run.plans.size(); ++d) {
    for (const auto& id : run.plans[d].postponed_order_ids) {
      CHECK(std::ranges::find(run.days[d + 1].orders, id, &Order::id) != run.days[d + 1].orders.end());
    }
  }
  // State: day d + 1 starts from the sum of the recomputed service minutes of days 0..d.
  for (std::size_t k = 0; k < w.drivers.size(); ++k) {
    Minutes sum = 0;
    for (std::size_t d = 0; d < run.days.size(); ++d) {
      CHECK(run.days[d].states[k].service_minutes_week == sum);
      sum += run.day_checks[d].drivers[k].service_minutes;
    }
  }
  const auto kp = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
  CHECK(kp.week_checker_ok);
  CHECK(kp.served + kp.unserved_end == static_cast<int>(w.orders.size()));
}
