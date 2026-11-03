// APP_PLAN A1/A2: timelines and run bundles of the visual application. The timeline is drawn from
// the same walk as the checker: its totals must equal the checker's recomputed facts on every
// duty, its segments must tile the duty, and its break counters must follow the law.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/viz/bundle.hpp"
#include "legalvrp/viz/gallery.hpp"
#include "legalvrp/domain/rules.hpp"
#include <fstream>
#include "legalvrp/viz/timeline.hpp"
#include "legalvrp/week/loop.hpp"

using namespace legalvrp;

namespace {

WeekInstance golden_small() { return data::read_week(fixtures_dir() / "instances" / "small" / "1"); }

void check_timelines(const week::WeekRun& run) {
  int duties = 0;
  for (std::size_t d = 0; d < run.plans.size(); ++d) {
    const DayInstance& day = run.days[d];
    const check::DayCheck& dc = run.day_checks[d];
    for (const Route& r : run.plans[d].routes) {
      if (r.order_ids.empty()) continue;
      ++duties;
      const viz::Timeline t = viz::build_timeline(day, r);
      INFO("day " << d << " driver " << r.driver_id);
      const auto f = std::ranges::find(dc.drivers, r.driver_id, &check::DriverFacts::driver_id);
      REQUIRE(f != dc.drivers.end());
      CHECK(t.driving == f->driving_minutes);
      CHECK(t.work == f->service_minutes);
      CHECK(std::abs(t.km - f->km) < 1e-9);
      CHECK(t.duty_start == f->duty_start);
      CHECK(t.duty_end == f->duty_end);
      REQUIRE(!t.segments.empty());
      CHECK(t.segments.front().start == t.duty_start);
      CHECK(t.segments.back().end == t.duty_end);
      for (std::size_t i = 1; i < t.segments.size(); ++i) CHECK(t.segments[i].start == t.segments[i - 1].end);
      for (const auto& s : t.segments) {
        CHECK(s.end > s.start);
        CHECK(s.drive_since_break <= day.rules.drive_before_break);  // legal plans only here
        CHECK(s.work_since_break <= day.rules.work_before_break);
      }
      const auto services = std::ranges::count(t.segments, std::string(viz::segment::service), &viz::Segment::kind);
      CHECK(services == static_cast<long>(r.order_ids.size()));
      CHECK(t.pallets <= day.truck(std::ranges::find(day.drivers, r.driver_id, &Driver::id)->truck_id).capacity_pallets);
    }
  }
  CHECK(duties > 10);
}

}  // namespace

TEST_CASE("timelines equal the checker's facts: baseline week", "[viz]") {
  const WeekInstance w = golden_small();
  const auto run = week::run_week(w, heuristics::territory_baseline);
  REQUIRE(run.week_check.ok());
  check_timelines(run);
}

TEST_CASE("timelines equal the checker's facts: ALNS week", "[viz]") {
  const WeekInstance w = golden_small();
  alns::Options ao;
  ao.max_iterations = 2000;
  ao.time_limit_s = 1e9;
  const auto run = week::run_week(w, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
  REQUIRE(run.week_check.ok());
  check_timelines(run);
}

TEST_CASE("break counters: a lone 30 resets work only, 15 + 30 resets driving", "[viz]") {
  const WeekInstance w = golden_small();
  const auto run = week::run_week(w, heuristics::territory_baseline);
  const DayInstance& day = run.days[0];
  Route r;
  for (const Route& x : run.plans[0].routes) {
    if (x.order_ids.size() >= 3) {
      r = x;
      break;
    }
  }
  REQUIRE(r.order_ids.size() >= 3);
  auto drive_after = [&](const viz::Timeline& t, const std::string& id) {
    const auto it = std::ranges::find_if(t.segments, [&](const viz::Segment& s) {
      return s.kind == viz::segment::rest && s.order_id == id;
    });
    REQUIRE(it != t.segments.end());
    return std::pair{it->drive_since_break, it->work_since_break};
  };
  // Times need not be consistent for the counters: the timeline recomputes them.
  r.breaks = {{r.order_ids[0], 30}};
  auto t = viz::build_timeline(day, r);
  auto [dr, wk] = drive_after(t, r.order_ids[0]);
  CHECK(dr > 0);
  CHECK(wk == 0);
  r.breaks = {{r.order_ids[0], 15}, {r.order_ids[1], 30}};
  t = viz::build_timeline(day, r);
  std::tie(dr, wk) = drive_after(t, r.order_ids[0]);
  CHECK(dr > 0);
  std::tie(dr, wk) = drive_after(t, r.order_ids[1]);
  CHECK(dr == 0);
  r.breaks = {{r.order_ids[1], 45}};
  t = viz::build_timeline(day, r);
  std::tie(dr, wk) = drive_after(t, r.order_ids[1]);
  CHECK(dr == 0);
  CHECK(t.breaks == 45);
}

TEST_CASE("run bundle: schema, KPIs, realised days and ALNS gap", "[viz]") {
  const WeekInstance w = golden_small();
  alns::Options ao;
  ao.max_iterations = 500;
  ao.time_limit_s = 1e9;
  const auto run = week::run_week(w, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
  const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
  const auto truth = data::read_truth(fixtures_dir() / "instances" / "small" / "1");
  viz::RunData rd;
  rd.run = "test";
  rd.solver = "alns";
  rd.week = &w;
  rd.days = &run.days;
  rd.plans = &run.plans;
  rd.day_checks = &run.day_checks;
  rd.week_check = &run.week_check;
  rd.kpis = &k;
  rd.truth = &truth;
  const auto b = viz::run_bundle(rd);
  CHECK(b["schema"] == viz::kBundleSchema);
  CHECK(b["days"].size() == static_cast<std::size_t>(w.days));
  CHECK(b["kpis"]["cost_total"].get<double>() == k.cost_total);
  CHECK(!b["instance"].contains("orders"));
  CHECK(b["instance"]["customers"].size() == w.customers.size());
  CHECK(b.contains("realised_kpis"));
  for (const auto& d : b["days"]) {
    CHECK(d["solver"]["gap"].is_null());  // ALNS has no bound: never "0 %"
    CHECK(d["check"]["ok"].get<bool>());
    CHECK(d["realised"]["routes"].size() == d["routes"].size());
    for (const auto& o : d["orders"]) CHECK(o.contains("service_true"));
  }
}

TEST_CASE("checker's gallery: every corpus duty's verdict equals its label", "[viz][corpus]") {
  std::ifstream in(fixtures_dir() / "duties.json");
  REQUIRE(in);
  const auto corpus = nlohmann::json::parse(in);
  const auto g = viz::duty_gallery(corpus, load_config());
  REQUIRE(g["items"].size() == corpus["duties"].size());
  for (const auto& it : g["items"]) {
    INFO(it["name"].get<std::string>());
    CHECK(it["matches"].get<bool>());
    if (!it["route"]["order_ids"].empty()) CHECK(it["timeline"].is_object());
  }
}
