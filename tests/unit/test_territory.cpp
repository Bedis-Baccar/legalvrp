// T5 acceptance: the territory baseline produces plans with zero checker violations on 20
// generated days. Plus: Hungarian vs brute force, k-means sanity, objective = checker's
// recomputation, 2-opt local optimality, equipment-aware assignment, postponement.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/assignment.hpp"
#include "legalvrp/heuristics/kmeans.hpp"
#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/week/certify.hpp"

using namespace legalvrp;
using namespace legalvrp::heuristics;

namespace {
const Config& config() {
  static const Config c = load_config();
  return c;
}
WeekInstance small_week(std::uint64_t seed) {
  return data::generate_week(data::load_instance_config(config_dir() / "instance_small.yaml"),
                             config(), seed).week;
}
}  // namespace

// ============================================================ Hungarian

TEST_CASE("Hungarian matches brute force on random rectangular matrices", "[assignment]") {
  std::uint64_t h = 99;
  auto next = [&] {
    h = h * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
    return (h >> 33) % 1000;
  };
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t rows = 1 + next() % 4;
    const std::size_t cols = rows + next() % 3;
    std::vector<std::vector<double>> c(rows, std::vector<double>(cols));
    for (auto& row : c) {
      for (auto& x : row) x = next() % 10 == 0 ? kForbidden : static_cast<double>(next() % 100);
    }
    // brute force over injective row -> column maps
    std::vector<std::size_t> perm(cols);
    std::iota(perm.begin(), perm.end(), 0);
    double best = std::numeric_limits<double>::max();
    do {
      double s = 0.0;
      for (std::size_t r = 0; r < rows; ++r) s += c[r][perm[r]];
      best = std::min(best, s);
    } while (std::next_permutation(perm.begin(), perm.end()));

    const auto a = min_cost_assignment(c);
    double got = 0.0;
    std::vector<bool> used(cols, false);
    for (std::size_t r = 0; r < rows; ++r) {
      if (!a[r]) {
        got += kForbidden;
        continue;
      }
      REQUIRE_FALSE(used[*a[r]]);
      used[*a[r]] = true;
      got += c[r][*a[r]];
    }
    INFO("trial " << trial);
    CHECK(got == Catch::Approx(best));
  }
}

// ============================================================ k-means

TEST_CASE("k-means separates obvious clusters, deterministically", "[kmeans]") {
  std::vector<Point2> pts;
  for (int i = 0; i < 5; ++i) pts.push_back({50.0 + i * 0.1, 0.0});
  for (int i = 0; i < 5; ++i) pts.push_back({-40.0, 30.0 + i * 0.1});
  for (int i = 0; i < 5; ++i) pts.push_back({0.0, -45.0 - i * 0.1});
  const auto a = kmeans(pts, 3);
  const auto b = kmeans(pts, 3);
  CHECK(a.label == b.label);
  for (int c = 0; c < 3; ++c) {
    const auto first = a.label[static_cast<std::size_t>(c * 5)];
    for (int i = 1; i < 5; ++i) CHECK(a.label[static_cast<std::size_t>(c * 5 + i)] == first);
  }
  CHECK(a.label[0] != a.label[5]);
  CHECK(a.label[5] != a.label[10]);
  CHECK(kmeans({}, 3).label.empty());
  CHECK(kmeans(pts, 40).centers.size() == pts.size());  // k clamped
}

// ============================================================ T5 acceptance

TEST_CASE("baseline: zero checker violations on 20 generated days", "[territory][acceptance]") {
  int days = 0;
  for (std::uint64_t seed = 1; seed <= 4; ++seed) {
    const WeekInstance w = small_week(seed);
    for (int d = 0; d < w.days; ++d) {
      const DayInstance day = make_day_instance(w, d);
      const DayPlan plan = territory_baseline(day);
      const auto r = check::check_day(day, plan);
      INFO("seed " << seed << " day " << d);
      CHECK(r.ok());
      CHECK(plan.objective == Catch::Approx(r.objective).epsilon(1e-9));  // = checker's recomputation
      ++days;
    }
  }
  CHECK(days == 20);
}

TEST_CASE("baseline over a rolling week passes the week-mode checker", "[territory][week]") {
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    const auto b = week::run_baseline_week(small_week(seed));
    INFO("seed " << seed);
    CHECK(b.check.ok());
    REQUIRE(b.plans.size() == 5);
    for (std::size_t d = 0; d < b.plans.size(); ++d) {
      CHECK(b.plans[d].objective == Catch::Approx(b.check.days[d].objective).epsilon(1e-9));
    }
  }
}

TEST_CASE("baseline routes are 2-opt local optima", "[territory]") {
  const WeekInstance w = small_week(2);
  for (int d = 0; d < w.days; ++d) {
    const DayInstance day = make_day_instance(w, d);
    const RouteEvaluator ev(day);
    const auto seq = territory_sequences(day);
    for (std::size_t k = 0; k < seq.routes.size(); ++k) {
      const auto& s = seq.routes[k];
      const double cost = ev.evaluate(k, s).cost;
      for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        for (std::size_t j = i + 1; j < s.size(); ++j) {
          auto t = s;
          std::reverse(t.begin() + static_cast<std::ptrdiff_t>(i), t.begin() + static_cast<std::ptrdiff_t>(j) + 1);
          const auto e = ev.evaluate(k, t);
          INFO("day " << d << " driver " << k << " reverse " << i << ".." << j);
          CHECK_FALSE((e.legal && e.cost < cost - 1e-9));
        }
      }
    }
  }
}

TEST_CASE("baseline is deterministic", "[territory]") {
  const DayInstance day = make_day_instance(small_week(3), 1);
  CHECK(nlohmann::json(territory_baseline(day)) == nlohmann::json(territory_baseline(day)));
}

// ============================================================ hand-built days

namespace {
DayInstance hand_day() {
  DayInstance d;
  d.rules = config().rules;
  d.contracts = config().contracts;
  d.costs = config().costs;
  d.depot = Depot{"depot", 0, 0};
  d.trucks = {Truck{"plain", 18, false}, Truck{"lift", 12, true}};
  d.drivers = {Driver{"k1", "full_time", "plain", 360, 1125, {0}},
               Driver{"k2", "full_time", "lift", 360, 1125, {0}}};
  d.states = {DriverWeekState{"k1", 0, 0, std::nullopt}, DriverWeekState{"k2", 0, 0, std::nullopt}};
  return d;
}
void add(DayInstance& d, const char* id, double x, double y, Minutes e, Minutes l, bool lift = false) {
  Customer c;
  c.id = id;
  c.x_km = x;
  c.y_km = y;
  c.window_start = e;
  c.window_end = l;
  c.needs_tail_lift = lift;
  d.customers.push_back(c);
  Order o;
  o.id = id;
  o.customer_id = id;
  o.pallets = 2;
  o.service_mu = 22;
  o.postpone_penalty = 200;
  d.orders.push_back(o);
}
void euclid_matrix(DayInstance& d) {
  Matrix& m = d.matrix;
  m.node_ids = {"depot"};
  std::vector<std::pair<double, double>> xy{{0, 0}};
  for (const auto& c : d.customers) {
    m.node_ids.push_back(c.id);
    xy.push_back({c.x_km, c.y_km});
  }
  const std::size_t n = xy.size();
  m.time_min.assign(n * n, 0);
  m.dist_km.assign(n * n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      const double dist = std::hypot(xy[i].first - xy[j].first, xy[i].second - xy[j].second);
      m.dist_km[i * n + j] = dist;
      m.time_min[i * n + j] = static_cast<Minutes>(std::ceil(dist)) + 4;
    }
  }
}
}  // namespace

TEST_CASE("tail-lift orders go to the tail-lift driver, nothing postponed", "[territory]") {
  DayInstance d = hand_day();
  add(d, "A", 20, 0, 360, 900, true);
  add(d, "B", 22, 2, 360, 900, true);
  add(d, "C", -20, 0, 360, 900);
  add(d, "D", -22, -2, 360, 900);
  euclid_matrix(d);
  const DayPlan p = territory_baseline(d);
  CHECK(check::check_day(d, p).ok());
  CHECK(p.postponed_order_ids.empty());
  for (const auto& r : p.routes) {
    for (const auto& id : r.order_ids) {
      if (id == "A" || id == "B") CHECK(r.driver_id == "k2");
    }
  }
}

TEST_CASE("an order nobody can reach in its window is postponed", "[territory]") {
  DayInstance d = hand_day();
  add(d, "A", 20, 0, 360, 900);
  add(d, "FAR", 300, 0, 360, 400);  // 304 min away, window closes at 06:40
  euclid_matrix(d);
  const DayPlan p = territory_baseline(d);
  CHECK(check::check_day(d, p).ok());
  CHECK(p.postponed_order_ids == std::vector<std::string>{"FAR"});
  CHECK(p.objective == Catch::Approx(check::check_day(d, p).objective));
}

TEST_CASE("no orders, or no drivers", "[territory]") {
  DayInstance d = hand_day();
  euclid_matrix(d);
  CHECK(territory_baseline(d).routes.empty());
  add(d, "A", 20, 0, 360, 900);
  euclid_matrix(d);
  d.drivers.clear();
  d.states.clear();
  CHECK(territory_baseline(d).postponed_order_ids == std::vector<std::string>{"A"});
}

// ============================================================ certification

TEST_CASE("certified instances: zero violations and postponed share within the limit", "[certify]") {
  const auto cfg = data::load_instance_config(config_dir() / "instance_small.yaml");
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    const auto c = week::generate_certified_week(cfg, config(), seed);
    INFO("seed " << seed);
    REQUIRE(c.has_value());
    CHECK(c->generated.week.certified);
    CHECK(c->baseline.check.ok());
    CHECK(c->baseline.max_postponed_share() <= cfg.max_postponed_share);
  }
}
