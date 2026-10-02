#include "legalvrp/data/synthetic.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#include "legalvrp/domain/json.hpp"

namespace legalvrp::data {

namespace {

struct Point {
  double x = 0.0;
  double y = 0.0;
};

// Uniform point in the annulus r_min <= |p| <= r_max, by rejection from the square.
// No trigonometry: sin/cos are not bit-identical across platforms (D-003).
Point in_annulus(Rng& rng, double r_min, double r_max) {
  for (;;) {
    const double x = rng.uniform(-r_max, r_max);
    const double y = rng.uniform(-r_max, r_max);
    const double r2 = x * x + y * y;
    if (r2 <= r_max * r_max && r2 >= r_min * r_min) return {x, y};
  }
}

std::string padded(const char* prefix, std::size_t n) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%s%03zu", prefix, n);
  return buf;
}

}  // namespace

double round_km(double x) noexcept { return std::round(x * 1000.0) / 1000.0; }

Geography make_geography(const InstanceConfig& cfg, Rng& rng) {
  Geography g;
  g.depot = Depot{"depot", 0.0, 0.0};

  const auto n_towns = static_cast<std::size_t>(rng.uniform_int(cfg.towns_min, cfg.towns_max));
  std::vector<Point> towns;
  std::vector<double> spreads;
  for (std::size_t t = 0; t < n_towns; ++t) {
    towns.push_back(in_annulus(rng, cfg.town_distance_min_km, cfg.town_distance_max_km));
    spreads.push_back(rng.uniform(cfg.spread_min_km, cfg.spread_max_km));
  }

  std::vector<double> shares;
  for (const auto& t : cfg.customer_types) shares.push_back(t.share);

  const auto pool =
      static_cast<std::size_t>(std::ceil(cfg.customer_pool_factor * cfg.orders_max));
  for (std::size_t i = 1; i <= pool; ++i) {
    const auto town = static_cast<std::size_t>(
        rng.uniform_int(0, static_cast<std::int64_t>(n_towns) - 1));
    const Point off = in_annulus(rng, 0.0, spreads[town]);
    const CustomerTypeParams& type = cfg.customer_types[rng.categorical(shares)];

    Customer c;
    c.id = padded("c", i);
    c.name = to_string(type.type) + " " + std::to_string(i);
    c.type = type.type;
    c.x_km = round_km(towns[town].x + off.x);
    c.y_km = round_km(towns[town].y + off.y);
    c.window_start = type.window_start;
    c.window_end = type.window_end;
    c.needs_tail_lift = rng.bernoulli(type.tail_lift_prob);
    g.customers.push_back(std::move(c));
  }
  return g;
}

}  // namespace legalvrp::data
