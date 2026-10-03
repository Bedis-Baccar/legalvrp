// V1-T3 (Gurobi): route-pool set partitioning. (1) With the optimal routes in the pool and the
// baseline as incumbent, it reaches the exact optimum (20 regime days). (2) ALNS + pool + polish
// is never worse than ALNS alone, legal, and the pool stays within its time budget.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/enumerate.hpp"
#include "legalvrp/heuristics/route_eval.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/pool/pool.hpp"

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

const Config& config() {
  static const Config c = load_config();
  return c;
}

DayInstance regime_day(std::uint64_t seed, bool work_heavy) {  // the V0 T6 regimes, n = 5, K = 2
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

TEST_CASE("pool reaches the optimum when the optimal routes are in it (20 regime days)", "[pool]") {
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      const auto ex = heuristics::solve_by_enumeration(day);
      const heuristics::RouteEvaluator ev(day);
      std::vector<alns::Column> cols;
      for (const auto& rt : ex.plan.routes) {
        alns::Column c;
        c.driver = static_cast<std::size_t>(std::ranges::find(day.drivers, rt.driver_id, &Driver::id) - day.drivers.begin());
        for (const auto& id : rt.order_ids) {
          c.seq.push_back(static_cast<std::size_t>(std::ranges::find(day.orders, id, &Order::id) - day.orders.begin()));
        }
        c.cost = ev.quick(c.driver, c.seq).cost;
        cols.push_back(c);
      }
      const auto base = heuristics::territory_sequences(day).routes;  // incumbent = baseline
      const auto r = pool::solve_pool(env(), day, cols, base);
      INFO((work ? "work" : "drive") << " seed " << seed);
      CHECK(r.status == "OPTIMAL");
      CHECK(r.cost == Catch::Approx(ex.objective).epsilon(1e-9));
      CHECK(r.cost <= r.incumbent_cost + 1e-6);
      const auto plan = alns::make_plan(day, r.routes, r.bank);
      CHECK(check::check_day(day, plan).ok());
    }
  }
}

TEST_CASE("ALNS + pool + polish: never worse than ALNS alone, legal, pool within budget", "[pool][hybrid]") {
  const auto small = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const auto lc = data::load_scale_config(config_dir() / "instance_large.yaml");
  const auto large = data::generate_week(data::scale_instance(lc, 60), config(), 1).week;
  int improved = 0, days = 0;
  for (const WeekInstance* w : {&small, &large}) {
    for (int d = 0; d < (w == &small ? w->days : 2); ++d) {
      const DayInstance day = make_day_instance(*w, d);
      pool::HybridOptions o;
      o.alns.max_iterations = w == &small ? 200 : 1500;
      o.alns.time_limit_s = 1e9;
      o.pool.time_limit_s = 30;
      o.polish_time_s = 1.0;
      const auto h = pool::solve_hybrid(env(), day, o);
      const auto chk = check::check_day(day, h.plan);
      INFO(w->name << " day " << d << ": ALNS " << h.first.cost << ", pool " << h.pool.cost << " (" << h.pool.columns
                   << " columns, " << h.pool.status << ", " << h.pool.runtime_s << " s), final " << h.cost);
      CHECK(chk.ok());
      CHECK(h.cost == Catch::Approx(chk.objective).epsilon(1e-9));
      CHECK(h.pool.cost <= h.first.cost + 1e-6);
      CHECK(h.cost <= h.first.cost + 1e-6);
      CHECK(h.pool.runtime_s <= 31.0);
      improved += h.cost < h.first.cost - 1e-6 ? 1 : 0;
      ++days;
    }
  }
  WARN("ALNS + pool + polish improved on the ALNS phase on " << improved << "/" << days << " days");
}
