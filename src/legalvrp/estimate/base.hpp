#pragma once
// legalvrp::estimate — Estimator protocol (§9).
// Given the orders and the customers they belong to, return the planning estimate of the
// service minutes (mean and standard deviation) per order.
// Implementations: DeterministicEstimator (V0: service_mu, sigma 0) and LearnedEstimator
// (V1-T7: fitted on history by customer type and pallets).

#include <span>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::estimate {

struct ServiceEstimate {
  double mu = 0.0;
  double sigma = 0.0;
};

class Estimator {
 public:
  virtual ~Estimator() = default;
  // One estimate per order, same order as `orders`.
  [[nodiscard]] virtual std::vector<ServiceEstimate> estimate(
      std::span<const Order> orders, std::span<const Customer> customers) const = 0;
};

// Planning durations for the whole week: service_mu := max(1, round(mu + z * sigma)) and
// service_sigma := round(sigma). Every solver and the checker then plan with the buffer.
void apply(WeekInstance& week, const Estimator& estimator, double z);

// Pooled time reserve (V1-T7, D-113): the planning instance keeps `reserve` minutes free before
// every limit a late stop can break — work blocks (WB), daily service, end of shift and of the
// daytime window, weekly service cap — and before every customer's window end. One reserve per
// duty instead of a buffer at every stop (delays of k stops add up like sqrt(k), not k).
// Plans made on the tightened instance must be judged against the real one.
void reserve_time(WeekInstance& week, Minutes reserve);

}  // namespace legalvrp::estimate
