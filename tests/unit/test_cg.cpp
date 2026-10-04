// V1-T9 (Gurobi): column generation lower bounds. The relaxed route cost never exceeds the true
// cost of a legal route; the labelling finds the exact minimum of the relaxation (checked by
// enumerating every sequence on small days); the bound never exceeds the exact optimum
// (enumeration) and the column generation's upper bound is a legal plan.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/cg/colgen.hpp"
#include "legalvrp/cg/pricing.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/enumerate.hpp"
#include "legalvrp/heuristics/route_eval.hpp"

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

// The T6 regime days (n = 5, K = 2), as in test_alns / test_milp.
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
      m.time_min[i * n + j] = static_cast<Minutes>(std::ceil(km / 55.0 * 60.0 * (1.0 + rng.uniform(-0.05, 0.05)))) + 4;
    }
  }
  return d;
}

// Every ordered sequence of distinct orders (n = 5: 325 sequences).
void sequences(std::size_t n, const std::function<void(const std::vector<std::size_t>&)>& f) {
  std::vector<std::size_t> seq;
  std::vector<bool> used(n, false);
  std::function<void()> rec = [&] {
    if (!seq.empty()) f(seq);
    for (std::size_t i = 0; i < n; ++i) {
      if (used[i]) continue;
      used[i] = true;
      seq.push_back(i);
      rec();
      seq.pop_back();
      used[i] = false;
    }
  };
  rec();
}

}  // namespace

TEST_CASE("relaxed cost <= true cost on every legal route (20 regime days, all sequences)", "[cg]") {
  int legal = 0;
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      const heuristics::RouteEvaluator ev(day);
      for (std::size_t k = 0; k < day.drivers.size(); ++k) {
        const cg::RelaxedPricer pr(day, k);
        const double idle = ev.quick(k, {}).cost;
        sequences(day.orders.size(), [&](const std::vector<std::size_t>& seq) {
          const auto q = ev.quick(k, seq);
          if (!q.legal) return;
          ++legal;
          INFO("seed " << seed << " driver " << k);
          CHECK(pr.relaxed_cost(seq) <= q.cost - idle + 1e-6);  // a relaxed route, never dearer
        });
      }
    }
  }
  CHECK(legal > 200);
}

TEST_CASE("labelling = minimum over every relaxed sequence (elementary ng); dynamic ng reaches it", "[cg]") {
  data::Rng rng(7);
  int grown = 0;
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      std::vector<double> pi(day.orders.size());
      for (auto& p : pi) p = rng.uniform(0, 400);  // duals that make routes attractive
      for (std::size_t k = 0; k < day.drivers.size(); ++k) {
        cg::RelaxedPricer pr(day, k);
        double best = 0.0;  // the empty route
        sequences(day.orders.size(), [&](const std::vector<std::size_t>& seq) {
          const double c = pr.relaxed_cost(seq);
          if (!std::isfinite(c)) return;
          double v = c;
          for (const auto i : seq) v -= pi[i];
          best = std::min(best, v);
        });
        const cg::PricingOptions po;
        INFO("seed " << seed << " driver " << k);
        pr.reset_ng(static_cast<int>(day.orders.size()));  // elementary
        const auto res = pr.price(pi, po);
        REQUIRE(res.exact);
        CHECK(res.min_value == Catch::Approx(best).margin(1e-6));
        // From neighbourhoods of size 1: never above; forbidding the cycles of the best routes
        // until they are elementary reaches the elementary minimum.
        pr.reset_ng(1);
        auto r1 = pr.price(pi, po);
        CHECK(r1.min_value <= best + 1e-6);
        for (int round = 0; round < 50; ++round) {
          bool grew = false;
          for (const auto& rr : r1.best) grew = pr.forbid_cycles(rr.seq) || grew;
          if (!grew) break;
          ++grown;
          r1 = pr.price(pi, po);
        }
        CHECK(r1.min_value == Catch::Approx(best).margin(1e-6));
      }
    }
  }
  CHECK(grown > 0);  // cycles did appear and were forbidden
}

TEST_CASE("CG lower bound <= exact optimum; CG upper bound is a legal plan (20 regime days)", "[cg]") {
  double gap_sum = 0.0;
  int tight = 0;
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      const double opt = heuristics::solve_by_enumeration(day).objective;
      alns::Options ao;
      ao.max_iterations = 200;
      ao.time_limit_s = 1e9;
      ao.collect_pool = true;
      const auto a = alns::solve(day, ao);
      cg::Options co;
      co.time_limit_s = 30;
      co.initial = a.pool;
      co.incumbent = a.routes;
      const auto r = cg::solve(env(), day, co);
      INFO((work ? "work-heavy" : "driving-heavy") << " seed " << seed);
      REQUIRE(r.bound_valid);
      CHECK(r.lower_bound <= opt + 1e-6);
      CHECK(r.upper_bound >= opt - 1e-6);
      const auto plan = alns::make_plan(day, r.routes, r.bank);
      const auto chk = check::check_day(day, plan);
      CHECK(chk.ok());
      CHECK(chk.objective == Catch::Approx(r.upper_bound).epsilon(1e-9));
      gap_sum += (opt - r.lower_bound) / opt;
      tight += opt - r.lower_bound < 1e-6 * opt ? 1 : 0;
    }
  }
  WARN("CG bound: mean gap to the optimum " << 100.0 * gap_sum / 20.0 << " %, equal on " << tight << "/20 days");
}

TEST_CASE("CG on a small fixture day: valid bound, below the ALNS cost", "[cg]") {
  const auto w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(w, 0);
  alns::Options ao;
  ao.max_iterations = 2000;
  ao.time_limit_s = 1e9;
  ao.collect_pool = true;
  const auto a = alns::solve(day, ao);
  cg::Options co;
  co.time_limit_s = 60;
  co.initial = a.pool;
  co.incumbent = a.routes;
  cg::Result r;
  try {
    r = cg::solve(env(), day, co);
  } catch (const GRBException& e) {
    FAIL("Gurobi error " << e.getErrorCode() << ": " << e.getMessage());
  }
  REQUIRE(r.bound_valid);
  CHECK(r.lower_bound <= a.cost + 1e-6);
  CHECK(r.upper_bound <= a.cost + 1e-6);
  CHECK(r.lower_bound <= r.lp_value + 1e-6);
  WARN("small day 0: ALNS " << a.cost << ", CG UB " << r.upper_bound << ", LB " << r.lower_bound << ", LP "
                            << r.lp_value << ", " << r.iterations << " iterations, " << r.columns << " columns, "
                            << r.runtime_s << " s, ng " << r.ng_used << ", converged " << r.converged);
}

TEST_CASE("diagnosis: CG bound vs the exact master LP over every legal route (regime days)", "[cg][diag]") {
  double gap_lb = 0.0, gap_lp = 0.0;
  for (const bool work : {true, false}) {
    for (std::uint64_t seed = 1; seed <= 10; ++seed) {
      const DayInstance day = regime_day((work ? 1000 : 2000) + seed, work);
      const double opt = heuristics::solve_by_enumeration(day).objective;
      const heuristics::RouteEvaluator ev(day);
      std::vector<alns::Column> all;  // every legal route: the exact master LP
      for (std::size_t k = 0; k < day.drivers.size(); ++k) {
        sequences(day.orders.size(), [&](const std::vector<std::size_t>& seq) {
          const auto q = ev.quick(k, seq);
          if (q.legal) all.push_back({k, seq, q.cost});
        });
      }
      cg::Options co;
      co.time_limit_s = 30;
      co.mip_time_s = 0;
      co.initial = all;
      const auto r = cg::solve(env(), day, co);
      REQUIRE(r.bound_valid);
      CHECK(r.lower_bound <= r.lp_value + 1e-6);
      CHECK(r.lp_value <= opt + 1e-6);
      gap_lb += (opt - r.lower_bound) / opt;
      gap_lp += (opt - r.lp_value) / opt;
    }
  }
  WARN("regime days: exact master LP " << 100.0 * gap_lp / 20 << " % below the optimum, CG bound "
                                       << 100.0 * gap_lb / 20 << " % below");
}

TEST_CASE("experiment: ng size on a small fixture day", "[.cgexp]") {
  const auto w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(w, 0);
  alns::Options ao;
  ao.max_iterations = 2000;
  ao.time_limit_s = 1e9;
  ao.collect_pool = true;
  const auto a = alns::solve(day, ao);
  for (const int ng : {4, 8, 12, 20}) {
    cg::Options co;
    co.time_limit_s = 120;
    co.mip_time_s = 0;
    co.pricing.ng = ng;
    co.pricing.max_labels = 3000000;
    co.initial = a.pool;
    co.incumbent = a.routes;
    const auto r = cg::solve(env(), day, co);
    WARN("ng " << ng << ": LB " << r.lower_bound << " (used " << r.ng_used << "), LP " << r.lp_value << ", "
               << r.iterations << " it, " << r.runtime_s << " s");
  }
}
