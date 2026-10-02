#pragma once
// legalvrp::data — distance and travel-time matrices (§5.1).
//   dist_ij = round_km(detour * euclid(i, j))                        symmetric, km
//   time_ij = ceil(dist_ij / speed * 60 * (1 + eps_ij)) + access     integer minutes, i != j
//   eps_ij ~ U(-noise, +noise), drawn independently per direction (asymmetric)
// Nodes: depot first, then customers in pool order. Diagonal = 0.

#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

[[nodiscard]] Matrix make_matrix(const Depot& depot, const std::vector<Customer>& customers,
                                 const InstanceConfig& cfg, Rng& rng);

}  // namespace legalvrp::data
