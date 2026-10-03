// T8: KPIs and report from the checker's recomputation; state helpers.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <set>

#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/kpi/report.hpp"
#include "legalvrp/week/loop.hpp"
#include "legalvrp/week/state.hpp"

using namespace legalvrp;
namespace fs = std::filesystem;

TEST_CASE("Gini coefficient on known values", "[kpi]") {
  CHECK(kpi::gini({}) == 0.0);
  CHECK(kpi::gini({0, 0, 0}) == 0.0);
  CHECK(kpi::gini({30, 30, 30}) == 0.0);
  CHECK(kpi::gini({0, 0, 0, 40}) == Catch::Approx(0.75));   // one does everything: (n-1)/n
  CHECK(kpi::gini({10, 20, 30}) == Catch::Approx(2.0 / 9.0));
}

TEST_CASE("baseline week: KPIs account for every order and match the checker", "[kpi][week]") {
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const auto run = week::run_week(w, heuristics::territory_baseline);
  REQUIRE(run.week_check.ok());
  const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);

  // Every order of the week is served exactly once or unserved at the end.
  CHECK(k.served + k.unserved_end == static_cast<int>(w.orders.size()));
  std::set<std::string> served;
  for (const auto& p : run.plans) {
    for (const auto& r : p.routes) {
      for (const auto& id : r.order_ids) CHECK(served.insert(id).second);
    }
  }
  CHECK(static_cast<int>(served.size()) == k.served);

  double cost = 0, km = 0;
  for (std::size_t d = 0; d < run.plans.size(); ++d) {
    cost += run.week_check.days[d].objective;
    for (const auto& f : run.week_check.days[d].drivers) km += f.km;
  }
  CHECK(k.cost_total == Catch::Approx(cost));
  CHECK(k.km == Catch::Approx(km));
  CHECK(k.on_time_rate == 1.0);
  CHECK(k.hours_gini >= 0.0);
  CHECK(k.hours_gini < 1.0);

  // Weekly totals per driver equal the checker's final state.
  for (const auto& dw : k.drivers) {
    const auto st = std::ranges::find(run.week_check.final_states, dw.driver_id, &DriverWeekState::driver_id);
    REQUIRE(st != run.week_check.final_states.end());
    CHECK(dw.service_minutes == st->service_minutes_week);
    CHECK(dw.driving_minutes == st->driving_minutes_week);
    CHECK(dw.extra_minutes == std::max(0, dw.service_minutes - dw.threshold));
  }
}

TEST_CASE("week report files are written", "[kpi]") {
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "tiny" / "1");
  const auto run = week::run_week(w, heuristics::territory_baseline);
  const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
  const fs::path dir = fs::temp_directory_path() / "legalvrp_t8_report";
  fs::remove_all(dir);
  kpi::write_week_report(dir, k, "test");
  CHECK(fs::file_size(dir / "week_kpis.json") > 0);
  const std::string md = data::read_text_file(dir / "week_report.md");
  CHECK(md.find("hours_gini") != std::string::npos);
  CHECK(md.find("OK, 0 violations") != std::string::npos);
  fs::remove_all(dir);
}

TEST_CASE("state helpers: carried orders keep their first due day", "[kpi][state]") {
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  DayPlan p;
  p.postponed_order_ids = {w.orders.front().id};
  const auto carried = week::carried_orders(w, p);
  REQUIRE(carried.size() == 1);
  CHECK(carried[0].postponed_from == w.orders.front().day);
  CHECK(carried[0].postpone_penalty == w.orders.front().postpone_penalty);  // base, not escalated
  p.postponed_order_ids = {"ghost"};
  CHECK_THROWS(week::carried_orders(w, p));
}
