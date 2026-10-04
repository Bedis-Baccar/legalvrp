// V1-T7: learned service-time estimator. Fit recovers a known model; quantiles are calibrated;
// planning durations mu + z sigma; JSON round trip; fallback for a type without history.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/estimate/deterministic.hpp"
#include "legalvrp/estimate/learned.hpp"

using namespace legalvrp;

namespace {

// n observations per type: pallets uniform in 1..8, minutes lognormal(a + b p, cv), rounded.
std::vector<estimate::Observation> synthetic(int n, std::uint64_t seed) {
  data::Rng rng(seed);
  std::vector<estimate::Observation> out;
  struct T {
    CustomerType type;
    double a, b, cv;
  };
  for (const T t : {T{CustomerType::grocery, 12, 6, 0.30}, T{CustomerType::restaurant, 14, 7, 0.45}}) {
    for (int i = 0; i < n; ++i) {
      const auto p = static_cast<Pallets>(rng.uniform_int(1, 8));
      out.push_back({t.type, p, data::sample_service(t.a + t.b * p, t.cv, rng)});
    }
  }
  return out;
}

}  // namespace

TEST_CASE("fit recovers the mean line and the cv; empirical quantiles are calibrated", "[estimate]") {
  const auto model = estimate::fit(synthetic(20000, 3));
  REQUIRE(model.types.size() == 2);  // no site observations: no site model
  const auto& g = model.types[0];
  const auto& r = model.types[1];
  CHECK(g.type == CustomerType::grocery);
  CHECK(g.fixed == Catch::Approx(12.0).margin(0.6));
  CHECK(g.per_pallet == Catch::Approx(6.0).margin(0.15));
  CHECK(g.cv == Catch::Approx(0.30).margin(0.02));
  CHECK(r.fixed == Catch::Approx(14.0).margin(0.8));
  CHECK(r.per_pallet == Catch::Approx(7.0).margin(0.2));
  CHECK(r.cv == Catch::Approx(0.45).margin(0.03));
  // Ratio quantiles: increasing; the median is below the mean (skewed, lognormal-like).
  REQUIRE(g.ratio_quantiles.size() == 4);
  for (std::size_t i = 1; i < g.ratio_quantiles.size(); ++i) {
    CHECK(g.ratio_quantiles[i].second > g.ratio_quantiles[i - 1].second);
  }
  CHECK(g.ratio_quantiles[0].second < 1.0);
  // Calibration on fresh data: about 90 % of observations below the 0.9 quantile.
  int below = 0, total = 0;
  for (const auto& o : synthetic(5000, 4)) {
    const auto& m = o.type == CustomerType::grocery ? g : r;
    below += static_cast<double>(o.minutes) <= m.mu(o.pallets) * m.ratio_quantiles[2].second ? 1 : 0;
    ++total;
  }
  CHECK(static_cast<double>(below) / total == Catch::Approx(0.90).margin(0.02));
}

TEST_CASE("learned estimates, planning durations mu + z sigma, fallback, JSON round trip", "[estimate]") {
  const auto model = estimate::fit(synthetic(5000, 5));
  const estimate::LearnedEstimator est(model);
  WeekInstance w;
  w.customers = {Customer{}, Customer{}, Customer{}};
  w.customers[0].id = "g";
  w.customers[0].type = CustomerType::grocery;
  w.customers[1].id = "r";
  w.customers[1].type = CustomerType::restaurant;
  w.customers[2].id = "s";
  w.customers[2].type = CustomerType::site;  // no history: keeps the planning rule
  w.orders = {Order{"o1", "g", 0, 3, 28, 0, std::nullopt, 200}, Order{"o2", "r", 0, 1, 16, 0, std::nullopt, 200},
              Order{"o3", "s", 0, 4, 34, 0, std::nullopt, 200}};
  const auto e = est.estimate(w.orders, w.customers);
  REQUIRE(e.size() == 3);
  CHECK(e[0].mu == Catch::Approx(model.types[0].fixed + 3 * model.types[0].per_pallet));
  CHECK(e[0].sigma == Catch::Approx(model.types[0].cv * e[0].mu));
  CHECK(e[2].mu == 34.0);
  CHECK(e[2].sigma == 0.0);

  WeekInstance a = w;
  estimate::apply(a, est, 1.0);
  CHECK(a.orders[0].service_mu == static_cast<Minutes>(std::floor(e[0].mu + e[0].sigma + 0.5)));
  CHECK(a.orders[0].service_sigma == static_cast<Minutes>(std::floor(e[0].sigma + 0.5)));
  CHECK(a.orders[2].service_mu == 34);
  WeekInstance b = w;
  estimate::apply(b, estimate::DeterministicEstimator{}, 2.0);  // sigma 0: unchanged
  for (std::size_t i = 0; i < w.orders.size(); ++i) CHECK(b.orders[i].service_mu == w.orders[i].service_mu);

  const auto j = estimate::to_json(model);
  CHECK(j.at("table").size() == model.types.size() * static_cast<std::size_t>(model.max_pallets));
  const auto back = estimate::model_from_json(j);
  REQUIRE(back.types.size() == model.types.size());
  for (std::size_t t = 0; t < back.types.size(); ++t) {
    CHECK(back.types[t].type == model.types[t].type);
    CHECK(back.types[t].fixed == model.types[t].fixed);
    CHECK(back.types[t].per_pallet == model.types[t].per_pallet);
    CHECK(back.types[t].cv == model.types[t].cv);
    CHECK(back.types[t].ratio_quantiles == model.types[t].ratio_quantiles);
  }
}

TEST_CASE("observations of a generated week: one per order, with its customer's type", "[estimate]") {
  const auto cfg = data::load_instance_config(config_dir() / "instance_small.yaml");
  const auto g = data::generate_week(cfg, load_config(), 7);
  const auto obs = estimate::observations(g.week, g.truth.minutes);
  REQUIRE(obs.size() == g.week.orders.size());
  for (std::size_t i = 0; i < obs.size(); ++i) {
    const auto& o = g.week.orders[i];
    const auto c = std::ranges::find(g.week.customers, o.customer_id, &Customer::id);
    CHECK(obs[i].type == c->type);
    CHECK(obs[i].pallets == o.pallets);
    CHECK(obs[i].minutes == g.truth.minutes.at(o.id));
  }
}

TEST_CASE("time reserve tightens every limit; plans made with it are legal against the real limits", "[estimate]") {
  const auto w = data::read_week(fixtures_dir() / "instances" / "small" / "1");
  WeekInstance t = w;
  estimate::reserve_time(t, 30);
  CHECK(t.rules.work_before_break == w.rules.work_before_break - 30);
  CHECK(t.rules.daily_service_max == w.rules.daily_service_max - 30);
  CHECK(t.rules.latest_duty_end == w.rules.latest_duty_end - 30);
  for (std::size_t k = 0; k < w.drivers.size(); ++k) CHECK(t.drivers[k].shift_end_max == w.drivers[k].shift_end_max - 30);
  for (std::size_t c = 0; c < w.customers.size(); ++c) {
    CHECK(t.customers[c].window_end == std::max(w.customers[c].window_start, w.customers[c].window_end - 30));
  }
  CHECK(t.rules.drive_before_break == w.rules.drive_before_break);  // driving is deterministic: untouched
  WeekInstance same = w;
  estimate::reserve_time(same, 0);
  CHECK(same.rules.work_before_break == w.rules.work_before_break);

  alns::Options o;
  o.max_iterations = 300;
  o.time_limit_s = 1e9;
  for (int d = 0; d < w.days; ++d) {
    const DayPlan plan = alns::solve(make_day_instance(t, d), o).plan;
    INFO("day " << d);
    CHECK(check::check_day(make_day_instance(t, d), plan).ok());
    CHECK(check::check_day(make_day_instance(w, d), plan).ok());  // the real limits are looser
  }
}
