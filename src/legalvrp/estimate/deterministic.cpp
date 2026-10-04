#include "legalvrp/estimate/deterministic.hpp"

namespace legalvrp::estimate {

std::vector<ServiceEstimate> DeterministicEstimator::estimate(
    std::span<const Order> orders, std::span<const Customer> /*customers*/) const {
  std::vector<ServiceEstimate> out;
  out.reserve(orders.size());
  for (const auto& o : orders) out.push_back({static_cast<double>(o.service_mu), 0.0});
  return out;
}

}  // namespace legalvrp::estimate
