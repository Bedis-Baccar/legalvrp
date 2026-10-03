// V1-T6: uncertain service times. Portable log / normal / lognormal; the generator's truth
// (reproducible per seed, independent of the week); the realised-day simulator judged by the
// checker.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/sim/realise.hpp"

using namespace legalvrp;

namespace {

const Config& config() {
  static const Config c = load_config();
  return c;
}

data::InstanceConfig small_cfg() { return data::load_instance_config(config_dir() / "instance_small.yaml"); }

std::map<std::string, Minutes> planned_minutes(const DayInstance& day) {
  std::map<std::string, Minutes> m;
  for (const auto& o : day.orders) m[o.id] = o.service_mu;
  return m;
}

}  // namespace

TEST_CASE("portable_log matches ln to 1e-14 (relative)", "[rng][sim]") {
  for (const double x : {1e-300, 1e-12, 0.001, 0.25, 0.5, 0.7071, 0.99, 1.0, 1.0001, 1.5, 2.0, 2.718281828, 10.0,
                         123.456, 1e6, 1e300}) {
    INFO("x = " << x);
    const double ref = std::log(x);
    CHECK(std::abs(data::portable_log(x) - ref) <= 1e-14 * std::max(1.0, std::abs(ref)));
  }
  CHECK(data::portable_log(1.0) == 0.0);
}

TEST_CASE("normal and lognormal moments", "[rng][sim]") {
  data::Rng rng(42);
  const int n = 200000;
  double s1 = 0, s2 = 0;
  for (int i = 0; i < n; ++i) {
    const double z = rng.normal();
    s1 += z;
    s2 += z * z;
  }
  CHECK(std::abs(s1 / n) < 0.01);
  CHECK(std::abs(s2 / n - 1.0) < 0.01);

  double l1 = 0, l2 = 0;
  for (int i = 0; i < n; ++i) {
    const double x = rng.lognormal(30.0, 0.4);
    CHECK(x > 0.0);
    l1 += x;
    l2 += x * x;
  }
  const double mean = l1 / n;
  const double sd = std::sqrt(l2 / n - mean * mean);
  CHECK(mean == Catch::Approx(30.0).epsilon(0.01));
  CHECK(sd / mean == Catch::Approx(0.4).epsilon(0.03));
  CHECK(rng.lognormal(17.0, 0.0) == 17.0);  // no variance: the mean, no draw
}

TEST_CASE("truth: reproducible per seed, independent of the planned week, V0 without a model", "[truth][sim]") {
  const auto cfg = small_cfg();
  const auto a = data::generate_week(cfg, config(), 1);
  const auto b = data::generate_week(cfg, config(), 1);
  const auto c = data::generate_week(cfg, config(), 2);
  CHECK(a.truth.minutes == b.truth.minutes);
  CHECK(a.truth.sigma == b.truth.sigma);
  CHECK(a.truth.minutes != c.truth.minutes);

  auto v0 = cfg;  // no truth model: truth = planning estimate, same week
  for (auto& t : v0.customer_types) {
    t.service_fixed = 10.0;
    t.service_per_pallet = 6.0;
    t.service_cv = 0.0;
  }
  const auto z = data::generate_week(v0, config(), 1);
  CHECK(nlohmann::json(z.week) == nlohmann::json(a.week));  // truth has its own stream
  int differ = 0;
  double ratio = 0.0;
  for (const auto& o : z.week.orders) {
    CHECK(z.truth.minutes.at(o.id) == o.service_mu);
    CHECK(z.truth.sigma.at(o.id) == 0.0);
    differ += a.truth.minutes.at(o.id) != o.service_mu ? 1 : 0;
    ratio += static_cast<double>(a.truth.minutes.at(o.id)) / a.truth.mean.at(o.id);
  }
  CHECK(differ > static_cast<int>(a.week.orders.size()) / 2);  // the model does perturb the truth
  CHECK(ratio / static_cast<double>(a.week.orders.size()) == Catch::Approx(1.0).epsilon(0.1));
}

TEST_CASE("realised day: truth = estimate reproduces the plan exactly", "[sim]") {
  const auto w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  for (int d = 0; d < w.days; ++d) {
    const DayInstance day = make_day_instance(w, d);
    alns::Options o;
    o.max_iterations = 200;
    o.time_limit_s = 1e9;
    const DayPlan plan = alns::solve(day, o).plan;
    const auto r = sim::realise_day(day, plan, planned_minutes(day));
    INFO("day " << d);
    CHECK(r.check.ok());
    CHECK(nlohmann::json(r.plan.routes) == nlohmann::json(plan.routes));
    for (const auto& duty : r.duties) {
      CHECK_FALSE(duty.late);
      CHECK_FALSE(duty.illegal);
      CHECK(duty.overrun == 0);
      CHECK(duty.max_delay == 0);
    }
  }
}

TEST_CASE("realised day: delays propagate, are absorbed by slack, and the checker judges", "[sim]") {
  const auto w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  const DayInstance day = make_day_instance(w, 0);
  alns::Options o;
  o.max_iterations = 200;
  o.time_limit_s = 1e9;
  const DayPlan plan = alns::solve(day, o).plan;
  REQUIRE_FALSE(plan.routes.empty());
  const Route& route = plan.routes.front();

  // +5 min at the first stop: the delay never grows, and later stops recover it in waiting slack.
  auto truth = planned_minutes(day);
  truth[route.order_ids.front()] += 5;
  const auto small = sim::realise_day(day, plan, truth);
  const auto& d1 = *std::ranges::find(small.duties, route.driver_id, &sim::DutyOutcome::driver_id);
  CHECK(d1.max_delay <= 5);
  CHECK(d1.overrun <= 5);
  CHECK(d1.overrun >= 0);

  // +600 min at the first stop: the duty cannot end in time -> late and illegal.
  truth = planned_minutes(day);
  truth[route.order_ids.front()] += 600;
  const auto big = sim::realise_day(day, plan, truth);
  const auto& d2 = *std::ranges::find(big.duties, route.driver_id, &sim::DutyOutcome::driver_id);
  CHECK(d2.illegal);
  CHECK(d2.overrun > 300);  // part of the delay may be absorbed by planned waiting
  CHECK_FALSE(big.check.ok());
  for (const auto& duty : big.duties) {  // other drivers are untouched
    if (duty.driver_id != route.driver_id) CHECK_FALSE(duty.illegal);
  }
  // Realised times are consistent: the checker reports no consistency problem.
  for (const auto& v : big.check.violations) CHECK(v.rule != std::string{rule::consistency});
}

TEST_CASE("sample_truth: deterministic per stream, centred on the true mean", "[sim]") {
  const auto g = data::generate_week(small_cfg(), config(), 1);
  data::Rng r1(7), r2(7);
  CHECK(sim::sample_truth(g.truth, r1) == sim::sample_truth(g.truth, r2));
  double sum = 0, mean = 0;
  data::Rng rng(11);
  for (int s = 0; s < 400; ++s) {
    for (const auto& [id, m] : sim::sample_truth(g.truth, rng)) sum += static_cast<double>(m);
  }
  for (const auto& [id, m] : g.truth.mean) mean += m;
  CHECK(sum / 400.0 == Catch::Approx(mean).epsilon(0.02));
}
