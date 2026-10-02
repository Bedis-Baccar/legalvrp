#pragma once
// legalvrp::data — daily orders (§5.1, D-011).
// Per day d: n_d = clamp(round(U{min..max} * weekday_factor[d]), min, max) orders, placed
// on n_d distinct customers drawn without replacement with weight = type.order_weight.
// pallets = min(1 + Poisson(type.pallets_lambda), largest truck); service_mu = 10 + 6 * pallets.
// Ids: d<day>-o<nn>, numbered in customer order. Each day uses its own RNG stream.

#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

[[nodiscard]] std::vector<Order> make_orders(const InstanceConfig& cfg,
                                             const std::vector<Customer>& customers,
                                             Pallets largest_truck, Euros postpone_penalty,
                                             const Rng& orders_stream);

[[nodiscard]] constexpr Minutes service_minutes(Pallets pallets) noexcept {
  return 10 + 6 * pallets;
}

}  // namespace legalvrp::data
