// T6 acceptance (Gurobi): tiny solves to optimality and passes the checker; the MILP matches the
// exact enumeration on 10 random days with n = 5, K = 2 in each of two regimes (work-heavy,
// driving-heavy) and on the trap fixtures (a) inflated driving and (b) break delay. Both
// formulations (strong = D-021, reference = brief §6) are checked.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>

#include "gurobi_c++.h"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/enumerate.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/model/solve.hpp"
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

const Config& config() {
  static const Config c = load_config();
  return c;
}

model::MilpOptions exact(model::Formulation f) {
  model::MilpOptions o;
  o.formulation = f;
  o.solver.time_limit = 60;
  o.solver.mip_gap = 0.0;
  o.solver.threads = 1;
  return o;
}

constexpr model::Formulation kBoth[] = {model::Formulation::strong, model::Formulation::reference};

// Objectives agree up to solver tolerance.
void same_objective(double milp, double reference) {
  CHECK(milp == Catch::Approx(reference).epsilon(1e-7).margin(1e-6));
}

DayInstance base_day() {
  DayInstance d;
  d.rules = config().rules;
  d.contracts = config().contracts;
  d.costs = config().costs;
  d.depot = Depot{"depot", 0, 0};
  return d;
}

// Matrix over depot + customers in order, from an explicit time table (km = minutes).
void set_matrix(DayInstance& d, const std::vector<std::vector<Minutes>>& t) {
  Matrix& m = d.matrix;
  m.node_ids = {"depot"};
  for (const auto& c : d.customers) m.node_ids.push_back(c.id);
  const std::size_t n = m.node_ids.size();
  m.time_min.assign(n * n, 0);
  m.dist_km.assign(n * n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < n; ++j) {
      m.time_min[i * n + j] = t[i][j];
      m.dist_km[i * n + j] = t[i][j];
    }
  }
}

void add_order(DayInstance& d, const std::string& id, Minutes e, Minutes l, Minutes s, Pallets q,
               bool lift, Euros penalty, double x = 0, double y = 0) {
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
  o.pallets = q;
  o.service_mu = s;
  o.postpone_penalty = penalty;
  d.orders.push_back(o);
}

// Random day of the T6 regimes: n = 5 orders, K = 2 drivers.
DayInstance regime_day(std::uint64_t seed, bool work_heavy) {
  data::Rng rng(seed);
  DayInstance d = base_day();
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
    add_order(d, "o" + std::to_string(i + 1), e, l, s, static_cast<Pallets>(rng.uniform_int(1, 5)),
              rng.bernoulli(0.2), rng.uniform(300, 1500), x, y);
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

// MILP (both formulations) vs enumeration on one day; returns the enumeration optimum.
double compare_with_enumeration(const DayInstance& day, const std::string& label) {
  const auto ref = heuristics::solve_by_enumeration(day);
  for (const auto f : kBoth) {
    INFO(label << " formulation " << model::to_string(f));
    const auto r = model::solve_day_milp(env(), day, exact(f));
    REQUIRE(r.stats.status == "OPTIMAL");
    REQUIRE(r.plan.has_value());
    same_objective(r.stats.objective, ref.objective);
    const auto chk = check::check_day(day, *r.plan);
    CHECK(chk.ok());
    same_objective(chk.objective, ref.objective);  // recomputed from the extracted routes
  }
  return ref.objective;
}

}  // namespace

TEST_CASE("tiny: both formulations optimal, legal, equal to enumeration", "[milp][tiny]") {
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "tiny" / "1");
  const DayInstance day = make_day_instance(w, 0);
  REQUIRE(day.orders.size() <= 8);
  compare_with_enumeration(day, "tiny");
}

TEST_CASE("work-heavy regime: MILP = enumeration on 10 random days", "[milp][brute]") {
  int served = 0;
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    const DayInstance day = regime_day(1000 + seed, true);
    compare_with_enumeration(day, "work-heavy seed " + std::to_string(seed));
    served += static_cast<int>(heuristics::solve_by_enumeration(day).plan.routes.size());
  }
  CHECK(served > 0);  // the regime must exercise routing, not only postponement
}

TEST_CASE("driving-heavy regime: MILP = enumeration on 10 random days", "[milp][brute]") {
  int breaks = 0;
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    const DayInstance day = regime_day(2000 + seed, false);
    compare_with_enumeration(day, "driving-heavy seed " + std::to_string(seed));
    for (const auto& r : heuristics::solve_by_enumeration(day).plan.routes) breaks += r.break_after_order_id ? 1 : 0;
  }
  CHECK(breaks > 0);  // the regime must exercise the break logic
}

TEST_CASE("trap (a): inflated driving - the optimum cannot serve all three", "[milp][trap]") {
  // One driver; legs 0->A 60, A->B 140, B->C 200, C->0 140: no break position splits the 540
  // minutes into two parts <= 270. Every other leg is 300 (> 270, never legal), so no other
  // order of the three is legal either. Wide windows, penalty 1000.
  DayInstance d = base_day();
  d.trucks = {Truck{"t1", 18, false}};
  d.drivers = {Driver{"k1", "full_time", "t1", 360, 1125, {0}}};
  d.states = {DriverWeekState{"k1", 0, 0, std::nullopt}};
  for (const char* id : {"A", "B", "C"}) add_order(d, id, 360, 1100, 10, 2, false, 1000);
  constexpr Minutes X = 300;
  set_matrix(d, {{0, 60, X, X}, {X, 0, 140, X}, {X, X, 0, 200}, {140, X, X, 0}});
  compare_with_enumeration(d, "trap a");
  for (const auto f : kBoth) {
    const auto r = model::solve_day_milp(env(), d, exact(f));
    REQUIRE(r.plan);
    CHECK_FALSE(r.plan->postponed_order_ids.empty());
  }
}

TEST_CASE("trap (b): break delay - A then B is feasible only if the 45 min are ignored", "[milp][trap]") {
  // 0->A 200, A->B 100, B->0 150 (450 driving): only a break after A splits it (200 | 250).
  // Leaving A at 590 + 45 reaches B at 735 > 700; without the break delay it would be 690.
  // B->A is 300 (illegal). So A and B cannot both be served; the optimum serves one.
  DayInstance d = base_day();
  d.trucks = {Truck{"t1", 18, false}};
  d.drivers = {Driver{"k1", "full_time", "t1", 360, 1125, {0}}};
  d.states = {DriverWeekState{"k1", 0, 0, std::nullopt}};
  add_order(d, "A", 360, 1100, 10, 2, false, 1000);
  add_order(d, "B", 640, 700, 10, 2, false, 1000);
  set_matrix(d, {{0, 200, 150}, {200, 0, 100}, {150, 300, 0}});
  compare_with_enumeration(d, "trap b");
  for (const auto f : kBoth) {
    const auto r = model::solve_day_milp(env(), d, exact(f));
    REQUIRE(r.plan);
    CHECK(r.plan->postponed_order_ids.size() == 1);
  }
}

TEST_CASE("small day end to end: checker OK, MILP never worse than the baseline", "[milp][small]") {
  const WeekInstance w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(w, 0);
  model::MilpOptions o;
  o.solver.time_limit = 60;
  o.solver.mip_gap = 0.01;
  const auto r = week::solve_day(env(), day, o);
  CHECK(r.check.ok());
  CHECK(r.baseline_check.ok());
  CHECK(r.milp.start_accepted);
  CHECK(r.source == week::PlanSource::milp);
  CHECK(r.plan.objective <= r.baseline.objective + 1e-6);
}
