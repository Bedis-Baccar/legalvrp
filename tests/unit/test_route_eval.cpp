// T3 acceptance: unit tests on hand-computed routes, including departure delay and best
// break position. Plus D-007 (waiting before the break) and an exhaustive brute-force
// cross-check on generated instances.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/route_eval.hpp"

using namespace legalvrp;
using heuristics::RouteEvaluation;
using heuristics::RouteEvaluator;

namespace {

// One driver (truck 18 pallets, no tail-lift, full_time), customers added one by one;
// each customer gets one order with the same id. Distances in km = minutes (simple costs).
// Rules, contracts and costs: the repository config (270/360/45/540/720, P = 20, R = 10).
struct Fixture {
  DayInstance day;

  explicit Fixture(Minutes shift_start = 360, Minutes shift_end = 1125) {
    const Config cfg = load_config();
    day.rules = cfg.rules;
    day.contracts = cfg.contracts;
    day.costs = cfg.costs;
    day.depot = Depot{"depot", 0, 0};
    day.trucks = {Truck{"t1", 18, false}};
    day.drivers = {Driver{"k1", "full_time", "t1", shift_start, shift_end, {0}}};
    day.states = {DriverWeekState{"k1", 0, 0, std::nullopt}};
  }

  void add(const char* id, Minutes e, Minutes l, Minutes s, Pallets q = 1, bool lift = false) {
    Customer c;
    c.id = id;
    c.window_start = e;
    c.window_end = l;
    c.needs_tail_lift = lift;
    day.customers.push_back(c);
    Order o;
    o.id = id;
    o.customer_id = id;
    o.pallets = q;
    o.service_mu = s;
    o.postpone_penalty = 200;
    day.orders.push_back(o);
  }

  // times[i][j], nodes = depot then customers in add() order.
  void matrix(const std::vector<std::vector<Minutes>>& times) {
    Matrix& m = day.matrix;
    m.node_ids = {"depot"};
    for (const auto& c : day.customers) m.node_ids.push_back(c.id);
    const std::size_t n = m.node_ids.size();
    REQUIRE(times.size() == n);
    m.time_min.assign(n * n, 0);
    m.dist_km.assign(n * n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t j = 0; j < n; ++j) {
        m.time_min[i * n + j] = times[i][j];
        m.dist_km[i * n + j] = times[i][j];
      }
    }
  }

  [[nodiscard]] RouteEvaluation eval(std::vector<std::size_t> seq) const {
    return RouteEvaluator(day).evaluate(0, seq);
  }
};

bool has_rule(const RouteEvaluation& ev, std::string_view rule) {
  return std::ranges::any_of(ev.violations, [&](const Violation& v) { return v.rule == rule; });
}

constexpr Minutes X = 999;  // unused legs

}  // namespace

// ============================================================ hand-computed routes

TEST_CASE("departure is delayed to absorb waiting; duty still starts at the shift start", "[route_eval]") {
  // S = 360, P = 20 -> t0 >= 380. A opens at 480, 30 min away: leave at 450, no waiting at A.
  // Fixed start (D-018): the duty runs from 360 whatever the departure.
  Fixture f;
  f.add("A", 480, 600, 20);
  f.matrix({{0, 30}, {30, 0}});
  const auto ev = f.eval({0});
  REQUIRE(ev.legal);
  CHECK(ev.route.departure == 450);
  CHECK(ev.route.arrivals == std::vector<Minutes>{480});
  CHECK(ev.route.service_starts == std::vector<Minutes>{480});
  CHECK(ev.route.return_time == 530);              // 480 + 20 + 30
  CHECK(ev.service_minutes == 180);                // (530 + 10) - 360
  CHECK_FALSE(ev.route.break_after_order_id);      // tie with a break: "no break" wins
  CHECK(ev.driving_minutes == 60);
  CHECK(ev.cost == Catch::Approx(1.60 * 60 + 0.01 * 180));
}

TEST_CASE("best break position: only after B splits 400 min of driving", "[route_eval]") {
  // Legs 100/100/100/100. Break after A: 100 | 300 (no). After B: 200 | 200 (yes). After C: 300 | 100 (no).
  Fixture f;
  f.add("A", 360, 1100, 10);
  f.add("B", 360, 1100, 10);
  f.add("C", 360, 1100, 10);
  f.matrix({{0, 100, X, X}, {X, 0, 100, X}, {X, X, 0, 100}, {100, X, X, 0}});
  const auto ev = f.eval({0, 1, 2});
  REQUIRE(ev.legal);
  CHECK(ev.route.break_after_order_id == "B");
  CHECK(ev.route.departure == 380);
  CHECK(ev.route.service_starts == std::vector<Minutes>{480, 590, 745});  // break 600-645
  CHECK(ev.route.return_time == 855);
  CHECK(ev.service_minutes == 460);                // 865 - 360 - 45
  CHECK(ev.driving_minutes == 400);
}

TEST_CASE("trap (a): inflated driving - no break position makes 60/140/200/140 legal", "[route_eval]") {
  Fixture f;
  f.add("A", 360, 1100, 10);
  f.add("B", 360, 1100, 10);
  f.add("C", 360, 1100, 10);
  f.matrix({{0, 60, X, X}, {X, 0, 140, X}, {X, X, 0, 200}, {140, X, X, 0}});
  const auto ev = f.eval({0, 1, 2});
  CHECK_FALSE(ev.legal);
  REQUIRE_FALSE(ev.violations.empty());
  for (const auto& v : ev.violations) {
    CHECK((v.rule == rule::drive_without_break || v.rule == rule::drive_before_break ||
           v.rule == rule::drive_after_break));
  }
}

TEST_CASE("D-007: legal only by waiting before the break node (REVIEW_V0 F1)", "[route_eval]") {
  // A fixed at 400 forces t0 = 380. Earliest-start puts the break at 450-495 and 385 min of
  // work after it. Starting B at 465 instead (25 min wait, counted as work) gives exactly 360.
  Fixture f;
  f.add("A", 400, 400, 20);
  f.add("B", 440, 480, 10);
  f.add("C", 800, 900, 30);
  f.matrix({{0, 20, X, X}, {X, 0, 20, X}, {X, X, 0, 30}, {40, X, X, 0}});
  const auto ev = f.eval({0, 1, 2});
  REQUIRE(ev.legal);
  CHECK(ev.route.break_after_order_id == "B");
  CHECK(ev.route.departure == 380);
  CHECK(ev.route.arrivals == std::vector<Minutes>{400, 440, 550});
  CHECK(ev.route.service_starts == std::vector<Minutes>{400, 465, 800});
  CHECK(ev.route.return_time == 870);
  // work after the break: (870 + 10) - (465 + 10 + 45) = 360
  CHECK((ev.route.return_time + 10) - (ev.route.service_starts[1] + 10 + 45) == 360);
  CHECK(ev.service_minutes == 475);  // 880 - 360 - 45
}

TEST_CASE("a break is needed for work even with little driving", "[route_eval]") {
  // 300 min service: whole duty = 20 + 30 + 300 + 30 + 10 = 390 > 360 without a break.
  Fixture f;
  f.add("A", 360, 1100, 300);
  f.matrix({{0, 30}, {30, 0}});
  const auto ev = f.eval({0});
  REQUIRE(ev.legal);
  CHECK(ev.route.break_after_order_id == "A");
  CHECK(ev.route.return_time == 785);  // 380 + 30 + 300 + 45 + 30
  CHECK(ev.service_minutes == 390);
}

TEST_CASE("driving boundary: 270 needs no break, 271 does", "[route_eval]") {
  {
    Fixture f;
    f.add("A", 360, 1100, 10);
    f.matrix({{0, 135}, {135, 0}});
    const auto ev = f.eval({0});
    REQUIRE(ev.legal);
    CHECK_FALSE(ev.route.break_after_order_id);
    CHECK(ev.driving_minutes == 270);
  }
  {
    Fixture f;
    f.add("A", 360, 1100, 10);
    f.matrix({{0, 136}, {135, 0}});
    const auto ev = f.eval({0});
    REQUIRE(ev.legal);
    CHECK(ev.route.break_after_order_id == "A");  // 136 | 135
    CHECK(ev.service_minutes == 311);             // (706 + 10) - 360 - 45
  }
}

TEST_CASE("equipment and capacity violations", "[route_eval]") {
  Fixture f;
  f.add("A", 360, 1100, 10, 12, true);  // needs a tail-lift; truck has none
  f.add("B", 360, 1100, 10, 7);         // 12 + 7 = 19 > 18
  f.matrix({{0, 30, 30}, {30, 0, 30}, {30, 30, 0}});
  const auto ev = f.eval({0, 1});
  CHECK_FALSE(ev.legal);
  CHECK(has_rule(ev, rule::tail_lift));
  REQUIRE(has_rule(ev, rule::capacity));
  CHECK(std::ranges::find(ev.violations, std::string{rule::capacity}, &Violation::rule)->amount == 1);
}

TEST_CASE("window or shift impossible -> schedule", "[route_eval]") {
  {
    Fixture f;  // A closes at 400; earliest arrival 380 + 100 = 480
    f.add("A", 360, 400, 10);
    f.matrix({{0, 100}, {100, 0}});
    const auto ev = f.eval({0});
    CHECK_FALSE(ev.legal);
    CHECK(has_rule(ev, rule::schedule));
  }
  {
    Fixture f(360, 600);  // must be back by 590; earliest return 380 + 60 + 200 + 60 = 700
    f.add("A", 360, 1100, 200);
    f.matrix({{0, 60}, {60, 0}});
    const auto ev = f.eval({0});
    CHECK_FALSE(ev.legal);
    CHECK(has_rule(ev, rule::schedule));
  }
}

TEST_CASE("weekly caps use the driver's state", "[route_eval]") {
  Fixture f;
  f.add("A", 480, 600, 20);
  f.matrix({{0, 30}, {30, 0}});  // theta 180, driving 60
  f.day.states[0].service_minutes_week = 3120 - 100;  // H^max full_time = 3120
  auto ev = f.eval({0});
  CHECK_FALSE(ev.legal);
  CHECK(has_rule(ev, rule::weekly_service_max));

  f.day.states[0] = DriverWeekState{"k1", 0, 3360 - 10, std::nullopt};  // WD = 3360
  ev = f.eval({0});
  CHECK_FALSE(ev.legal);
  REQUIRE(has_rule(ev, rule::weekly_drive_max));
  CHECK(std::ranges::find(ev.violations, std::string{rule::weekly_drive_max}, &Violation::rule)->amount == 50);
}

TEST_CASE("cost: overtime above the weekly threshold; idle driver", "[route_eval]") {
  Fixture f;
  f.add("A", 480, 600, 20);
  f.matrix({{0, 30}, {30, 0}});
  f.day.states[0].service_minutes_week = 2340 - 50;  // 50 min below the 39 h threshold
  const auto ev = f.eval({0});
  REQUIRE(ev.legal);
  CHECK(ev.cost == Catch::Approx(1.60 * 60 + 0.01 * 180 + 0.45 * 130));  // 130 min overtime

  const auto idle = f.eval({});
  CHECK(idle.legal);
  CHECK(idle.service_minutes == 0);
  CHECK(idle.cost == 0.0);
  f.day.states[0].service_minutes_week = 2340 + 100;  // already above: constant term (Â§6.3)
  CHECK(f.eval({}).cost == Catch::Approx(0.45 * 100));
}

// ============================================================ brute-force cross-check

namespace {

// Naive exact reference: every integer departure minute x every start time at the break node,
// earliest-start everywhere else. Returns min theta, or nullopt if no legal schedule.
std::optional<Minutes> brute_force_theta(const DayInstance& day, std::size_t k,
                                         const std::vector<std::size_t>& seq) {
  const Driver& drv = day.drivers[k];
  const Rules& r = day.rules;
  const Contract& c = day.contract(drv.contract_class);
  const DriverWeekState& st = day.state(drv.id);
  const Truck& truck = day.truck(drv.truck_id);
  const Matrix& m = day.matrix;
  const std::size_t n = seq.size();
  const std::size_t depot = m.index_of(day.depot.id);

  Pallets load = 0;
  std::vector<std::size_t> node(n);
  std::vector<const Customer*> cust(n);
  for (std::size_t i = 0; i < n; ++i) {
    const Order& o = day.orders[seq[i]];
    cust[i] = &day.customer(o.customer_id);
    node[i] = m.index_of(cust[i]->id);
    load += o.pallets;
    if (cust[i]->needs_tail_lift && !truck.has_tail_lift) return std::nullopt;
  }
  if (load > truck.capacity_pallets) return std::nullopt;

  std::vector<Minutes> leg(n + 1);
  for (std::size_t i = 0; i <= n; ++i) {
    leg[i] = m.time(i == 0 ? depot : node[i - 1], i < n ? node[i] : depot);
  }
  Minutes total = 0;
  for (const auto x : leg) total += x;
  if (total > r.daily_drive_max || st.driving_minutes_week + total > r.weekly_drive_max) return std::nullopt;

  std::optional<Minutes> best;
  for (int b = -1; b < static_cast<int>(n); ++b) {
    Minutes before = 0;
    for (int i = 0; i <= b; ++i) before += leg[static_cast<std::size_t>(i)];
    if (b < 0 && total > r.drive_before_break) continue;
    if (b >= 0 && (before > r.drive_before_break || total - before > r.drive_before_break)) continue;

    for (Minutes t0 = drv.shift_start + r.depot_prep; t0 <= drv.shift_end_max; ++t0) {
      // earliest schedule up to the break node (or to the end)
      std::vector<Minutes> T(n);
      Minutes ready = t0;
      bool ok = true;
      const std::size_t upto = b < 0 ? n : static_cast<std::size_t>(b) + 1;
      for (std::size_t i = 0; i < upto && ok; ++i) {
        T[i] = std::max(ready + leg[i], cust[i]->window_start);
        ok = T[i] <= cust[i]->window_end;
        ready = T[i] + day.orders[seq[i]].service_mu;
      }
      if (!ok) continue;
      // candidate starts at the break node: earliest .. window end
      const Minutes tb_lo = b < 0 ? 0 : T[static_cast<std::size_t>(b)];
      const Minutes tb_hi = b < 0 ? 0 : cust[static_cast<std::size_t>(b)]->window_end;
      for (Minutes tb = tb_lo; tb <= tb_hi; ++tb) {
        std::vector<Minutes> U = T;
        Minutes rdy = ready;
        if (b >= 0) {
          const auto bb = static_cast<std::size_t>(b);
          U[bb] = tb;
          rdy = tb + day.orders[seq[bb]].service_mu + r.break_length;
        }
        bool ok2 = true;
        for (std::size_t i = upto; i < n && ok2; ++i) {
          U[i] = std::max(rdy + leg[i], cust[i]->window_start);
          ok2 = U[i] <= cust[i]->window_end;
          rdy = U[i] + day.orders[seq[i]].service_mu;
        }
        if (!ok2) break;  // later start only makes later windows worse
        const Minutes tE = rdy + leg[n];
        if (tE > drv.shift_end_max - r.depot_close) break;
        const Minutes duty_start = drv.shift_start;  // fixed start (D-018)
        const Minutes duty_end = tE + r.depot_close;
        bool legal = true;
        Minutes theta = duty_end - duty_start;
        if (b < 0) {
          legal = theta <= r.work_before_break;
        } else {
          const Minutes a = U[static_cast<std::size_t>(b)] + day.orders[seq[static_cast<std::size_t>(b)]].service_mu;
          legal = a - duty_start <= r.work_before_break &&
                  duty_end - (a + r.break_length) <= r.work_before_break;
          theta -= r.break_length;
        }
        legal = legal && theta <= r.daily_service_max &&
                st.service_minutes_week + theta <= c.weekly_service_max;
        if (legal) {
          if (!best || theta < *best) best = theta;
          break;  // theta is non-decreasing in the break start: first legal is best for this t0
        }
      }
    }
  }
  return best;
}

}  // namespace

namespace {
// Independent consistency check of a legal route the evaluator returned.
void check_route_is_legal(const DayInstance& day, std::size_t k, const std::vector<std::size_t>& seq,
                          const RouteEvaluation& ev) {
  const Route& rt = ev.route;
  const Rules& r = day.rules;
  const Driver& drv = day.drivers[k];
  const Matrix& m = day.matrix;
  const std::size_t depot = m.index_of(day.depot.id);
  REQUIRE(rt.service_starts.size() == seq.size());
  CHECK(rt.departure >= drv.shift_start + r.depot_prep);
  CHECK(rt.return_time + r.depot_close <= drv.shift_end_max);
  Minutes ready = rt.departure;
  std::size_t prev = depot;
  Minutes drive = 0, drive_at_break = 0;
  std::optional<Minutes> break_start;
  for (std::size_t i = 0; i < seq.size(); ++i) {
    const Order& o = day.orders[seq[i]];
    const Customer& c = day.customer(o.customer_id);
    const std::size_t node = m.index_of(c.id);
    drive += m.time(prev, node);
    CHECK(rt.arrivals[i] == ready + m.time(prev, node));
    CHECK(rt.service_starts[i] >= rt.arrivals[i]);
    CHECK(rt.service_starts[i] >= c.window_start);
    CHECK(rt.service_starts[i] <= c.window_end);
    ready = rt.service_starts[i] + o.service_mu;
    if (rt.break_after_order_id == o.id) {
      break_start = ready;
      drive_at_break = drive;
      ready += r.break_length;
    }
    prev = node;
  }
  drive += m.time(prev, depot);
  CHECK(rt.return_time == ready + m.time(prev, depot));
  CHECK(drive == ev.driving_minutes);
  const Minutes duty_start = drv.shift_start;  // fixed start (D-018)
  const Minutes duty_end = rt.return_time + r.depot_close;
  if (break_start) {
    CHECK(drive_at_break <= r.drive_before_break);
    CHECK(drive - drive_at_break <= r.drive_before_break);
    CHECK(*break_start - duty_start <= r.work_before_break);
    CHECK(duty_end - (*break_start + r.break_length) <= r.work_before_break);
    CHECK(ev.service_minutes == duty_end - duty_start - r.break_length);
  } else {
    CHECK(drive <= r.drive_before_break);
    CHECK(duty_end - duty_start <= r.work_before_break);
    CHECK(ev.service_minutes == duty_end - duty_start);
  }
}
}  // namespace

TEST_CASE("evaluator matches exhaustive brute force on generated routes", "[route_eval][brute]") {
  const Config cfg = load_config();
  const auto icfg = data::load_instance_config(config_dir() / "instance_small.yaml");
  int legal_count = 0;
  int compared = 0;
  int with_break = 0;
  for (const std::uint64_t seed : {4ULL, 9ULL}) {
  const auto week = data::generate_week(icfg, cfg, seed).week;
  for (int d = 0; d < week.days; ++d) {
    const DayInstance day = make_day_instance(week, d);
    const RouteEvaluator ev(day);
    std::uint64_t h = 12345 + seed * 100 + static_cast<std::uint64_t>(d);
    for (int trial = 0; trial < 40; ++trial) {
      // deterministic pseudo-random sequence of 1..4 distinct orders
      std::vector<std::size_t> seq;
      h = h * 6364136223846793005ULL + 1442695040888963407ULL;
      const std::size_t len = 1 + (h >> 33) % 4;
      while (seq.size() < len) {
        h = h * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::size_t i = (h >> 33) % day.orders.size();
        if (std::ranges::find(seq, i) == seq.end()) seq.push_back(i);
      }
      const std::size_t k = static_cast<std::size_t>(trial) % day.drivers.size();
      const auto got = ev.evaluate(k, seq);
      const auto ref = brute_force_theta(day, k, seq);
      INFO("day " << d << " trial " << trial << " driver " << k);
      REQUIRE(got.legal == ref.has_value());
      if (ref) {
        CHECK(got.service_minutes == *ref);
        check_route_is_legal(day, k, seq, got);
        ++legal_count;
        if (got.route.break_after_order_id) ++with_break;
      }
      ++compared;
    }
  }
  }
  CHECK(compared == 400);
  WARN("brute-force cross-check: " << legal_count << " legal of " << compared << ", "
       << with_break << " with a break");
  CHECK(legal_count >= 50);   // must exercise legal routes, not only rejections
  CHECK(with_break >= 5);     // and the break logic
}
