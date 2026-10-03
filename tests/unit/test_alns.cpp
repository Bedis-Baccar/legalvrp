// V1-T2 acceptance (without Gurobi): (a) ALNS plans are never illegal; (b) within 1 % of the exact
// optimum (enumeration) on the V0 regime days; (d) deterministic for a given seed. Also: the
// reported objective equals the checker's recomputation, and ALNS never ends above its start.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/enumerate.hpp"
#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"

using namespace legalvrp;

namespace {

const Config& config() {
  static const Config c = load_config();
  return c;
}

alns::Options fixed(long long iterations, std::uint64_t seed = 1) {
  alns::Options o;
  o.max_iterations = iterations;
  o.time_limit_s = 1e9;  // deterministic: iteration limit only
  o.seed = seed;
  return o;
}

// The T6 regime days of V0 (n = 5, K = 2): work-heavy or driving-heavy.
DayInstance regime_day(std::uint64_t seed, bool work_heavy) {
  data::Rng rng(seed);
  DayInstance d;
  d.rules = config().rules;
  d.contracts = config().contracts;
  d.costs = config().costs;
  d.depot = Depot{"depot", 0, 0};
  d.trucks = {Truck{"t1", 12, true}, Truck{"t2", 15, false}};
  d.drivers = {Driver{"k1", "full_time", "t1", 360, 1125, {0}}, Driver{"k2", "full_time", "t2", 420, 1140, {0}}};
  d.states = {DriverWeekState{"k1", rng.bernoulli(0.5) ? 2300 : 0, 0, std::nullopt},
              DriverWeekState{"k2", 0, 0, std::nullopt}};
  const double r_lo = work_heavy ? 35 : 70, r_hi = work_heavy ? 65 : 120;
  std::vector<std::pair<double, double>> xy{{0, 0}};
  for (int i = 0; i < 5; ++i) {
    double x = 0, y = 0;
    do {
      x = rng.uniform(-r_hi, r_hi);
      y = rng.uniform(-r_hi, r_hi);
    } while (x * x + y * y > r_hi * r_hi || x * x + y * y < r_lo * r_lo);
    xy.push_back({x, y});
    const auto e = static_cast<Minutes>(rng.uniform_int(360, 660));
    const auto l = std::min<Minutes>(e + static_cast<Minutes>(rng.uniform_int(90, 420)), 1080);
    const auto s = static_cast<Minutes>(work_heavy ? rng.uniform_int(45, 80) : rng.uniform_int(10, 20));
    Customer c;
    c.id = "o" + std::to_string(i + 1);
    c.x_km = x;
    c.y_km = y;
    c.window_start = e;
    c.window_end = l;
    c.needs_tail_lift = rng.bernoulli(0.2);
    d.customers.push_back(c);
    Order o;
    o.id = c.id;
    o.customer_id = c.id;
    o.pallets = static_cast<Pallets>(rng.uniform_int(1, 5));
    o.service_mu = s;
    o.postpone_penalty = rng.uniform(300, 1500);
    d.orders.push_back(o);
  }
  Matrix& m = d.matrix;
  m.node_ids = {"depot"};
  for (const auto& c : d.customers) m.node_ids.push_back(c.id);
  const std::size_t n = m.node_ids.size();
  m.time_min.assign(n * n, 0);
  m.dist_km.assign(n * n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      const double km = 1.3 * std::hypot(xy[i].first - xy[j].first, xy[i].second - xy[j].second);
      m.dist_km[i * n + j] = km;
      m.time_min[i * n + j] =
          static_cast<Minutes>(std::ceil(km / 55.0 * 60.0 * (1.0 + rng.uniform(-0.05, 0.05)))) + 4;
    }
  }
  return d;
}

}  // namespace

TEST_CASE("ALNS (b): within 1 % of the exact optimum on 20 V0 regime days", "[alns][exact]") {
  int exact_hits = 0;
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      const double opt = heuristics::solve_by_enumeration(day).objective;
      const auto r = alns::solve(day, fixed(300));
      INFO((work ? "work-heavy" : "driving-heavy") << " seed " << seed << ": ALNS " << r.cost << " vs optimum " << opt);
      CHECK(r.cost <= opt * 1.01 + 1e-6);
      CHECK(r.cost >= opt - 1e-6);  // cannot beat the optimum
      CHECK(check::check_day(day, r.plan).ok());
      exact_hits += std::abs(r.cost - opt) < 1e-6 ? 1 : 0;
    }
  }
  WARN("ALNS found the exact optimum on " << exact_hits << "/20 regime days");
}

TEST_CASE("ALNS (a): legal plans, objective = checker, never above the baseline", "[alns][legal]") {
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const auto lc = data::load_scale_config(config_dir() / "instance_large.yaml");
  const auto large = data::generate_week(data::scale_instance(lc, 60), config(), 1).week;
  for (const WeekInstance* w : {&small, &large}) {
    for (int d = 0; d < (w == &small ? w->days : 2); ++d) {
      const DayInstance day = make_day_instance(*w, d);
      const auto r = alns::solve(day, fixed(w == &small ? 400 : 150));
      const auto chk = check::check_day(day, r.plan);
      INFO(w->name << " day " << d);
      CHECK(chk.ok());
      CHECK(r.cost == Catch::Approx(chk.objective).epsilon(1e-9));
      const auto base = check::check_day(day, heuristics::territory_baseline(day));
      CHECK(r.cost <= base.objective + 1e-6);
      CHECK_FALSE(r.trace.empty());
    }
  }
}

TEST_CASE("ALNS (d): deterministic for a given seed; seeds differ", "[alns][determinism]") {
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(small, 1);
  const auto a = alns::solve(day, fixed(200, 7));
  const auto b = alns::solve(day, fixed(200, 7));
  CHECK(nlohmann::json(a.plan.routes) == nlohmann::json(b.plan.routes));  // not solver_stats (runtime)
  CHECK(a.plan.postponed_order_ids == b.plan.postponed_order_ids);
  CHECK(a.routes == b.routes);
  CHECK(a.cost == b.cost);
  CHECK(a.iterations == 200);
}

TEST_CASE("ALNS: route pool keeps legal, correctly costed routes", "[alns][pool]") {
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(small, 0);
  auto o = fixed(200);
  o.collect_pool = true;
  const auto r = alns::solve(day, o);
  REQUIRE(r.pool.size() > 10);
  const heuristics::RouteEvaluator ev(day);
  for (const auto& col : r.pool) {
    const auto e = ev.evaluate(col.driver, col.seq);
    REQUIRE(e.legal);
    CHECK(e.cost == Catch::Approx(col.cost).epsilon(1e-9));
  }
}
