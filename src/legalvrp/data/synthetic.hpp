#pragma once
// legalvrp::data — mode A geography (§5.1): depot at (0,0), 3-4 towns at 15-60 km,
// customers in a 3-6 km disc around their town. Coordinates rounded to 1 m.

#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

struct Geography {
  Depot depot;
  std::vector<Customer> customers;  // ids c001, c002, ... (pool shared by all days)
};

[[nodiscard]] Geography make_geography(const InstanceConfig& cfg, Rng& rng);

// Rounds to 3 decimals (1 m) with exact arithmetic, so stored values = used values.
[[nodiscard]] double round_km(double x) noexcept;

}  // namespace legalvrp::data
