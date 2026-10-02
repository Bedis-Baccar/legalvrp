#pragma once
// legalvrp::estimate — DeterministicEstimator: the generator's service_mu, sigma = 0 (§9).

#include "legalvrp/estimate/base.hpp"

namespace legalvrp::estimate {

class DeterministicEstimator final : public Estimator {
 public:
  [[nodiscard]] std::vector<ServiceEstimate> estimate(
      std::span<const Order> orders, std::span<const Driver> roster) const override;
};

}  // namespace legalvrp::estimate
