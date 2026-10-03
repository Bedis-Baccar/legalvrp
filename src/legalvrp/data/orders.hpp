#pragma once
// legalvrp::data — daily orders (§5.1, D-011).
// Per day d: n_d = clamp(round(U{min..max} * weekday_factor[d]), min, max) orders, placed
// on n_d distinct customers drawn without replacement with weight = type.order_weight.
// pallets = min(1 + Poisson(type.pallets_lambda), largest truck); service_mu = 10 + 6 * pallets.
// Ids: d<day>-o<nn>, numbered in customer order. Each day uses its own RNG stream.
//
// True service minutes (V1-T6, truth.json, generator side only): per order, lognormal with
// mean = type.service_fixed + type.service_per_pallet * pallets and cv = type.service_cv,
// rounded to whole minutes (>= 1). The planner keeps service_mu = 10 + 6 * pallets.

#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

[[nodiscard]] std::vector<Order> make_orders(const InstanceConfig& cfg,
                                             const std::vector<Customer>& customers,
                                             Pallets largest_truck, Euros postpone_penalty,
                                             const Rng& orders_stream);

// Truth of the orders of a week: draws from `truth_stream` in order sequence.
[[nodiscard]] TrueService make_truth(const InstanceConfig& cfg, const std::vector<Customer>& customers,
                                     const std::vector<Order>& orders, Rng truth_stream);

// True mean service minutes of `pallets` for a customer type.
[[nodiscard]] double true_service_mean(const CustomerTypeParams& type, Pallets pallets) noexcept;

// One realisation: lognormal(mean, cv) rounded, at least 1 minute (round(mean) when cv = 0).
[[nodiscard]] Minutes sample_service(double mean, double cv, Rng& rng) noexcept;

[[nodiscard]] constexpr Minutes service_minutes(Pallets pallets) noexcept {
  return 10 + 6 * pallets;
}

}  // namespace legalvrp::data
