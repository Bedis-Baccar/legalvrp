// V1-T2 acceptance (Gurobi): on the `small` days the MILP solves to optimality (gap <= 1 %), ALNS
// is within 1 % of the MILP objective; on the others it is reported against the incumbent.
#include <catch2/catch_test_macros.hpp>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/model/solve.hpp"

using namespace legalvrp;

TEST_CASE("ALNS within 1 % of the MILP optimum on the small days it solves", "[alns][milp]") {
  GRBEnv env(true);
  env.set(GRB_IntParam_OutputFlag, 0);
  env.start();
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  int compared = 0;
  for (int d = 0; d < w.days; ++d) {
    const DayInstance day = make_day_instance(w, d);
    model::MilpOptions mo;
    mo.solver.time_limit = 60;
    mo.solver.mip_gap = 0.01;
    const auto m = model::solve_day_milp(env, day, mo);
    alns::Options ao;
    ao.max_iterations = 3000;
    ao.time_limit_s = 1e9;
    const auto a = alns::solve(day, ao);
    CHECK(check::check_day(day, a.plan).ok());
    REQUIRE(m.plan.has_value());
    WARN("day " << d << ": MILP " << m.stats.status << " " << m.stats.objective << " (bound " << m.stats.best_bound
                << "), ALNS " << a.cost << " in " << a.runtime_s << " s");
    if (m.stats.status == "OPTIMAL") {
      CHECK(a.cost <= m.stats.objective * 1.01 + 1e-6);
      ++compared;
    }
    CHECK(a.cost >= m.stats.best_bound - 1e-6);  // never below a valid lower bound
  }
  CHECK(compared >= 2);
}
