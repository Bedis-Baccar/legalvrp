// T11 (Gurobi): clairvoyant weekly MILP. A hand-built two-day week where myopia provably costs:
//   k1 (cheap salaried driver, tail-lift, overtime threshold 300 min at 2 EUR/min)
//   k2 (temp: 0.55 EUR/min + 60 EUR day fee, no tail-lift)
//   Monday: order A (any truck), 270 min of duty.  Tuesday: order B (tail-lift), 270 min.
// Rolling: Monday k1 is cheaper (194.70 vs 400.50); Tuesday only k1 can serve B and crosses the
// threshold by 240 min (480 EUR): 869.40. Clairvoyant: k2 on Monday, k1 on Tuesday: 595.20.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gurobi_c++.h"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/model/clairvoyant.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/lookahead.hpp"
#include "legalvrp/week/loop.hpp"
#include "legalvrp/week/solve_day.hpp"

using namespace legalvrp;

namespace {

GRBEnv& env() {
  static GRBEnv e = [] {
    GRBEnv x(true);
    x.set(GRB_IntParam_OutputFlag, 0);
    x.start();
    return x;
  }();
  return e;
}

WeekInstance myopia_trap() {
  const Config cfg = load_config();
  WeekInstance w;
  w.name = "trap";
  w.days = 2;
  w.rules = cfg.rules;
  w.costs = cfg.costs;  // 1.60 EUR/km, penalties 200 / x2 / 1000
  w.contracts = {Contract{"cheap", 300, 3000, 0.01, 2.0, 0.0}, Contract{"temp", 3000, 3000, 0.55, 0.55, 60.0}};
  w.depot = Depot{"depot", 0, 0};
  w.trucks = {Truck{"t1", 18, true}, Truck{"t2", 18, false}};
  w.drivers = {Driver{"k1", "cheap", "t1", 360, 1125, {0, 1}}, Driver{"k2", "temp", "t2", 360, 1125, {0, 1}}};
  for (const auto& [id, lift] : {std::pair{"A", false}, std::pair{"B", true}}) {
    Customer c;
    c.id = id;
    c.window_start = 360;
    c.window_end = 1100;
    c.needs_tail_lift = lift;
    w.customers.push_back(c);
  }
  w.orders = {Order{"A", "A", 0, 2, 120, 0, std::nullopt, 200.0}, Order{"B", "B", 1, 2, 120, 0, std::nullopt, 200.0}};
  Matrix& m = w.matrix;
  m.node_ids = {"depot", "A", "B"};
  m.time_min = {0, 60, 60, 60, 0, 100, 60, 100, 0};
  m.dist_km = {0, 60, 60, 60, 0, 100, 60, 100, 0};
  return w;
}

model::MilpOptions exact() {
  model::MilpOptions o;
  o.solver.time_limit = 60;
  o.solver.mip_gap = 0.0;
  return o;
}

}  // namespace

TEST_CASE("myopia trap: clairvoyant 595.20 vs rolling 869.40", "[clairvoyant]") {
  const WeekInstance w = myopia_trap();
  const auto rolling = week::run_week(w, [&](const DayInstance& d) { return week::solve_day(env(), d, exact()).plan; });
  REQUIRE(rolling.week_check.ok());
  const auto kr = kpi::compute_week_kpis(w, rolling.days, rolling.plans, rolling.week_check);
  CHECK(kr.cost_total == Catch::Approx(869.40));
  CHECK(kr.extra_minutes_total == 240);

  const auto cl = model::solve_week_clairvoyant(env(), w, exact(), &rolling.plans);
  CHECK(cl.start_accepted);
  CHECK(cl.stats.status == "OPTIMAL");
  CHECK(cl.stats.objective == Catch::Approx(595.20));
  std::size_t next = 0;
  const auto crun = week::run_week(w, [&](const DayInstance&) { return cl.plans[next++]; });
  REQUIRE(crun.week_check.ok());
  const auto kc = kpi::compute_week_kpis(w, crun.days, crun.plans, crun.week_check);
  CHECK(kc.cost_total == Catch::Approx(595.20));  // model objective = checker's true weekly cost
  CHECK(kc.extra_minutes_total == 0);
  REQUIRE(cl.plans[0].routes.size() == 1);
  CHECK(cl.plans[0].routes[0].driver_id == "k2");
}

TEST_CASE("clairvoyant on a reduced generated week: legal, never above rolling, objective = KPI", "[clairvoyant]") {
  const auto ic = data::load_instance_config(config_dir() / "instance_myopia.yaml");
  const auto cw = week::generate_certified_week(ic, load_config(), 1);
  REQUIRE(cw.has_value());
  const WeekInstance& w = cw->generated.week;
  model::MilpOptions day = exact();
  day.solver.time_limit = 20;
  day.solver.mip_gap = 0.001;
  const auto rolling = week::run_week(w, [&](const DayInstance& d) { return week::solve_day(env(), d, day).plan; });
  REQUIRE(rolling.week_check.ok());
  const auto kr = kpi::compute_week_kpis(w, rolling.days, rolling.plans, rolling.week_check);

  model::MilpOptions wk;
  wk.solver.time_limit = 60;
  wk.solver.mip_gap = 0.005;
  const auto cl = model::solve_week_clairvoyant(env(), w, wk, &rolling.plans);
  CHECK(cl.start_accepted);
  std::size_t next = 0;
  const auto crun = week::run_week(w, [&](const DayInstance&) { return cl.plans[next++]; });
  CHECK(crun.week_check.ok());
  const auto kc = kpi::compute_week_kpis(w, crun.days, crun.plans, crun.week_check);
  CHECK(kc.cost_total <= kr.cost_total + 1e-6);
  CHECK(kc.cost_total == Catch::Approx(cl.stats.objective).epsilon(1e-6));
}

// ---- V1-T5: window model and week policies -------------------------------------------------

TEST_CASE("look-ahead (h = 2) on the trap: Monday sees Tuesday's tail-lift order (595.20)", "[lookahead]") {
  const WeekInstance w = myopia_trap();
  week::LookaheadOptions lo;
  lo.horizon = 2;
  lo.day = exact();
  lo.window = exact();
  std::vector<week::LookaheadSource> src;
  const auto run = week::run_week_with_context(w, [&](const DayInstance& d, const week::DayContext& ctx) {
    auto r = week::solve_day_lookahead(env(), w, d, ctx, lo);
    src.push_back(r.source);
    return r.plan;
  });
  REQUIRE(run.week_check.ok());
  const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
  CHECK(k.cost_total == Catch::Approx(595.20));
  REQUIRE(src.size() == 2);
  CHECK(src[0] == week::LookaheadSource::window);
  CHECK(src[1] == week::LookaheadSource::myopic);  // last day: no window needed
  REQUIRE(run.plans[0].routes.size() == 1);
  CHECK(run.plans[0].routes[0].driver_id == "k2");
}

TEST_CASE("a one-day window from the rolling state and carry-over = the daily MILP", "[window]") {
  const auto ic = data::load_instance_config(config_dir() / "instance_myopia.yaml");
  const auto cw = week::generate_certified_week(ic, load_config(), 2);
  REQUIRE(cw.has_value());
  const WeekInstance& w = cw->generated.week;
  model::MilpOptions o = exact();
  o.solver.time_limit = 30;
  int with_state = 0;
  const auto run = week::run_week_with_context(w, [&](const DayInstance& d, const week::DayContext& ctx) {
    const auto daily = week::solve_day(env(), d, o);
    const auto win = model::solve_window(env(), w, model::WindowSpec{d.day, d.day, ctx.states, ctx.carried, {}, 0.0}, o);
    INFO("day " << d.day);
    CHECK(win.stats.status == "OPTIMAL");
    CHECK(win.stats.objective == Catch::Approx(daily.check.objective).epsilon(1e-6));
    REQUIRE(win.plans.size() == 1);
    CHECK(check::check_day(d, win.plans[0]).ok());  // zero-state part + linking = the real day
    for (const auto& s : ctx.states) with_state += s.service_minutes_week > 0 ? 1 : 0;
    return daily.plan;
  });
  CHECK(run.week_check.ok());
  CHECK(with_state > 0);  // the comparison did cover non-zero weekly states
}

TEST_CASE("scarce_reserve: exclusive orders per known day x duty x days left", "[lookahead]") {
  WeekInstance w = myopia_trap();
  CHECK(week::scarce_reserve(w, 0).empty());  // A: both drivers can serve it
  CHECK(week::scarce_reserve(w, 1).empty());  // no day left after the window
  w.days = 3;
  const auto r = week::scarce_reserve(w, 1);  // B (tail-lift): only k1; 60 + 120 + 60 min over 2 days, 1 day left
  REQUIRE(r.size() == 1);
  CHECK(r[0].first == "k1");
  CHECK(r[0].second == Catch::Approx(120.0));
}
