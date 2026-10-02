// T4: plan-level checks (coverage, drivers), recomputed objective, week mode (daily rest,
// carry-over with escalating penalty, weekly state), and agreement with the route evaluator.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/route_eval.hpp"

using namespace legalvrp;

namespace {

bool has(const std::vector<Violation>& vs, std::string_view rule, const std::string& order = "") {
  return std::ranges::any_of(vs, [&](const Violation& v) {
    return v.rule == rule && (order.empty() || v.order_id == order);
  });
}

// Two drivers, three customers 30 min from the depot and from each other.
DayInstance small_day() {
  const Config cfg = load_config();
  DayInstance d;
  d.rules = cfg.rules;
  d.contracts = cfg.contracts;
  d.costs = cfg.costs;
  d.depot = Depot{"depot", 0, 0};
  d.trucks = {Truck{"t1", 18, false}, Truck{"t2", 18, false}};
  d.drivers = {Driver{"k1", "full_time", "t1", 360, 1125, {0, 1}},
               Driver{"k2", "full_time", "t2", 360, 1125, {0, 1}}};
  d.states = {DriverWeekState{"k1", 0, 0, std::nullopt}, DriverWeekState{"k2", 0, 0, std::nullopt}};
  for (const char* id : {"A", "B", "C"}) {
    Customer c;
    c.id = id;
    c.window_start = 360;
    c.window_end = 1100;
    d.customers.push_back(c);
    Order o;
    o.id = id;
    o.customer_id = id;
    o.pallets = 2;
    o.service_mu = 22;
    o.postpone_penalty = 200;
    d.orders.push_back(o);
  }
  Matrix& m = d.matrix;
  m.node_ids = {"depot", "A", "B", "C"};
  m.time_min.assign(16, 30);
  m.dist_km.assign(16, 30.0);
  for (std::size_t i = 0; i < 4; ++i) {
    m.time_min[i * 4 + i] = 0;
    m.dist_km[i * 4 + i] = 0.0;
  }
  return d;
}

// k1 serves A then B (legal); C as given by the caller.
Route route_ab() {
  // dep 380 -> A 410..432 -> B 462..484 -> depot 514
  return Route{"k1", {"A", "B"}, std::nullopt, 380, {410, 462}, {410, 462}, 514};
}

}  // namespace

// ============================================================ plan level

TEST_CASE("a legal plan has no violations and a recomputed objective", "[checker]") {
  const DayInstance d = small_day();
  DayPlan p;
  p.routes = {route_ab()};
  p.postponed_order_ids = {"C"};
  const auto r = check::check_day(d, p);
  CHECK(r.ok());
  REQUIRE(r.drivers.size() == 2);
  CHECK(r.drivers[0].used);
  CHECK(r.drivers[0].service_minutes == 524 - 360);  // fixed start: (514 + 10) - 360
  CHECK(r.drivers[0].driving_minutes == 90);
  CHECK_FALSE(r.drivers[1].used);
  CHECK(r.postponement_cost == 200.0);
  CHECK(r.objective == Catch::Approx(1.60 * 90 + 0.01 * 164 + 200.0));
}

TEST_CASE("coverage: missing, duplicated, unknown, served and postponed", "[checker]") {
  const DayInstance d = small_day();
  DayPlan missing;
  missing.routes = {route_ab()};  // C neither served nor postponed
  CHECK(has(check::check_day(d, missing).violations, rule::coverage, "C"));

  DayPlan both;
  both.routes = {route_ab()};
  both.postponed_order_ids = {"C", "A"};  // A served and postponed
  CHECK(has(check::check_day(d, both).violations, rule::coverage, "A"));

  DayPlan unknown;
  unknown.routes = {route_ab()};
  unknown.postponed_order_ids = {"C", "Z"};
  CHECK(has(check::check_day(d, unknown).violations, rule::coverage, "Z"));
}

TEST_CASE("one route per known driver", "[checker]") {
  const DayInstance d = small_day();
  DayPlan twice;
  Route again = route_ab();
  again.order_ids = {"C"};
  twice.routes = {route_ab(), again};
  CHECK(has(check::check_day(d, twice).violations, rule::consistency));

  DayPlan stranger;
  Route r = route_ab();
  r.driver_id = "k9";
  stranger.routes = {r};
  stranger.postponed_order_ids = {"C"};
  CHECK(has(check::check_day(d, stranger).violations, rule::consistency));
}

TEST_CASE("route arrays must match the sequence", "[checker]") {
  const DayInstance d = small_day();
  DayPlan p;
  Route r = route_ab();
  r.service_starts.pop_back();
  p.routes = {r};
  p.postponed_order_ids = {"C"};
  CHECK(has(check::check_day(d, p).violations, rule::consistency));
}

// ============================================================ week mode

namespace {
WeekInstance two_day_week(Minutes shift_start, Minutes shift_end) {
  const DayInstance d = small_day();
  WeekInstance w;
  w.name = "test";
  w.days = 2;
  w.depot = d.depot;
  w.customers = d.customers;
  w.trucks = {d.trucks[0]};
  w.drivers = {Driver{"k1", "full_time", "t1", shift_start, shift_end, {0, 1}}};
  w.matrix = d.matrix;
  w.rules = d.rules;
  w.contracts = d.contracts;
  w.costs = d.costs;
  for (auto o : d.orders) {  // A, B on Monday; C on Tuesday
    o.day = o.id == "C" ? 1 : 0;
    w.orders.push_back(o);
  }
  return w;
}
}  // namespace

TEST_CASE("week: postponed order carried with escalated penalty, state accumulates", "[checker][week]") {
  const WeekInstance w = two_day_week(360, 1125);
  DayPlan mon;
  mon.day = 0;
  mon.routes = {Route{"k1", {"A"}, std::nullopt, 380, {410}, {410}, 462}};
  mon.postponed_order_ids = {"B"};
  DayPlan tue;
  tue.day = 1;
  // Tuesday: B (carried) and C; postpone C on the last day -> unserved, 1000 EUR.
  tue.routes = {Route{"k1", {"B"}, std::nullopt, 380, {410}, {410}, 462}};
  tue.postponed_order_ids = {"C"};

  const auto r = check::check_week(w, {mon, tue});
  CHECK(r.ok());
  REQUIRE(r.days.size() == 2);
  CHECK(r.days[0].postponement_cost == 200.0);
  CHECK(r.days[1].postponement_cost == 1000.0);       // last day: unserved_end_penalty
  CHECK(r.unserved_order_ids == std::vector<std::string>{"C"});
  REQUIRE(r.final_states.size() == 1);
  CHECK(r.final_states[0].service_minutes_week == 2 * (472 - 360));
  CHECK(r.final_states[0].driving_minutes_week == 120);
}

TEST_CASE("week: a carried order must be covered the next day", "[checker][week]") {
  const WeekInstance w = two_day_week(360, 1125);
  DayPlan mon;
  mon.day = 0;
  mon.routes = {Route{"k1", {"A"}, std::nullopt, 380, {410}, {410}, 462}};
  mon.postponed_order_ids = {"B"};
  DayPlan tue;
  tue.day = 1;
  tue.postponed_order_ids = {"C"};  // forgets B
  const auto r = check::check_week(w, {mon, tue});
  REQUIRE(has(r.violations, rule::coverage, "B"));
  CHECK(std::ranges::find(r.violations, 1, &Violation::day) != r.violations.end());
}

TEST_CASE("week: daily rest of 11 h between consecutive duties", "[checker][week]") {
  // Shift 05:00-19:00 (wider than the generator allows). Monday: leave 1048, A 1078..1100,
  // break to 1145, back 1175, duty ends 1185. Tuesday starts 05:00 (1740 absolute):
  // 555 min of rest < 660. Monday also breaks other rules; the point here is the rest.
  const WeekInstance w = two_day_week(300, 1140);
  DayPlan mon;
  mon.day = 0;
  mon.routes = {Route{"k1", {"A"}, std::string{"A"}, 1048, {1078}, {1078}, 1175}};
  mon.postponed_order_ids = {"B"};
  DayPlan tue;
  tue.day = 1;
  tue.routes = {Route{"k1", {"B"}, std::nullopt, 320, {350}, {360}, 412}};
  tue.postponed_order_ids = {"C"};
  const auto r = check::check_week(w, {mon, tue});
  CHECK(has(r.violations, rule::daily_rest_min));
}

// ============================================================ agreement with the evaluator

TEST_CASE("every route the evaluator calls legal passes the checker with the same facts", "[checker][agreement]") {
  const Config cfg = load_config();
  const auto week = data::generate_week(
      data::load_instance_config(config_dir() / "instance_small.yaml"), cfg, 6).week;
  int legal = 0;
  for (int day_index = 0; day_index < week.days; ++day_index) {
    const DayInstance day = make_day_instance(week, day_index);
    const heuristics::RouteEvaluator ev(day);
    std::uint64_t h = 777 + static_cast<std::uint64_t>(day_index);
    for (int trial = 0; trial < 60; ++trial) {
      std::vector<std::size_t> seq;
      h = h * 6364136223846793005ULL + 1442695040888963407ULL;
      const std::size_t len = 1 + (h >> 33) % 5;
      while (seq.size() < len) {
        h = h * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::size_t i = (h >> 33) % day.orders.size();
        if (std::ranges::find(seq, i) == seq.end()) seq.push_back(i);
      }
      const std::size_t k = static_cast<std::size_t>(trial) % day.drivers.size();
      const auto e = ev.evaluate(k, seq);
      if (!e.legal) continue;
      ++legal;

      DayPlan p;
      p.day = day_index;
      p.routes = {e.route};
      for (const auto& o : day.orders) {
        if (std::ranges::find(e.route.order_ids, o.id) == e.route.order_ids.end()) {
          p.postponed_order_ids.push_back(o.id);
        }
      }
      const auto r = check::check_day(day, p);
      INFO("day " << day_index << " trial " << trial);
      REQUIRE(r.ok());
      const auto& f = r.drivers[k];
      CHECK(f.service_minutes == e.service_minutes);
      CHECK(f.driving_minutes == e.driving_minutes);
      CHECK(f.km == Catch::Approx(e.km));
      CHECK(f.cost == Catch::Approx(e.cost));
    }
  }
  CHECK(legal >= 50);
}
