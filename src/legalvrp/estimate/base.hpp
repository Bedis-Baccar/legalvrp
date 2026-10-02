#pragma once
// legalvrp::estimate — Estimator protocol (§9, ML readiness without ML).
// Given the day's orders and roster, return service_mu and service_sigma per order.
// V0 has one implementation: DeterministicEstimator (sigma = 0).

#include <span>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::estimate {

struct ServiceEstimate {
  Minutes mu = 0;
  Minutes sigma = 0;
};

class Estimator {
 public:
  virtual ~Estimator() = default;
  // One estimate per order, same order as `orders`.
  [[nodiscard]] virtual std::vector<ServiceEstimate> estimate(
      std::span<const Order> orders, std::span<const Driver> roster) const = 0;
};

}  // namespace legalvrp::estimate
