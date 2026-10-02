#include "legalvrp/data/matrix.hpp"

#include <cmath>

#include "legalvrp/data/synthetic.hpp"

namespace legalvrp::data {

Matrix make_matrix(const Depot& depot, const std::vector<Customer>& customers,
                   const InstanceConfig& cfg, Rng& rng) {
  std::vector<double> xs{depot.x_km};
  std::vector<double> ys{depot.y_km};
  Matrix m;
  m.node_ids.push_back(depot.id);
  for (const auto& c : customers) {
    m.node_ids.push_back(c.id);
    xs.push_back(c.x_km);
    ys.push_back(c.y_km);
  }
  const std::size_t n = m.node_ids.size();
  m.time_min.assign(n * n, 0);
  m.dist_km.assign(n * n, 0.0);

  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = i + 1; j < n; ++j) {
      const double dx = xs[i] - xs[j];
      const double dy = ys[i] - ys[j];
      const double d = round_km(cfg.detour_factor * std::sqrt(dx * dx + dy * dy));
      m.dist_km[i * n + j] = d;
      m.dist_km[j * n + i] = d;
    }
  }
  for (std::size_t i = 0; i < n; ++i) {  // fixed row-major draw order
    for (std::size_t j = 0; j < n; ++j) {
      if (i == j) continue;
      const double eps = rng.uniform(-cfg.asymmetric_noise, cfg.asymmetric_noise);
      const double minutes = m.dist_km[i * n + j] / cfg.speed_kmh * 60.0 * (1.0 + eps);
      m.time_min[i * n + j] =
          static_cast<Minutes>(std::ceil(minutes)) + cfg.access_minutes_per_leg;
    }
  }
  return m;
}

}  // namespace legalvrp::data
