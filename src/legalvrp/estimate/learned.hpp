#pragma once
// legalvrp::estimate — learned service-time estimator (V1-T7).
//
// Trained offline on history: the observed service minutes of past weeks, never the weeks being
// planned. Model per customer type t (pallets p):
//   mean       mu_t(p)    = a_t + b_t * p      least squares on the observed minutes
//   spread     sigma_t(p) = cv_t * mu_t(p)     cv_t = RMS of (y / mu_t(p) - 1)
//   quantiles  mu_t(p) * r_t(level)            r_t: empirical quantiles of y / mu_t(p)
// No distribution is assumed for the mean and the spread; the quantiles are empirical.
// Exported as a table per type x pallets (JSON); a type without history falls back to the
// order's own service_mu with sigma 0.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "legalvrp/estimate/base.hpp"

namespace legalvrp::estimate {

struct Observation {
  CustomerType type = CustomerType::grocery;
  Pallets pallets = 0;
  Minutes minutes = 0;
};

struct TypeModel {
  CustomerType type = CustomerType::grocery;
  std::size_t n = 0;
  double fixed = 0.0;       // a_t
  double per_pallet = 0.0;  // b_t
  double cv = 0.0;
  std::vector<std::pair<double, double>> ratio_quantiles;  // (level, r_t(level))
  [[nodiscard]] double mu(Pallets p) const noexcept;
  [[nodiscard]] double sigma(Pallets p) const noexcept { return cv * mu(p); }
};

struct LearnedModel {
  std::vector<TypeModel> types;
  std::string source;       // e.g. "instance_small.yaml, history seeds 101-120"
  Pallets max_pallets = 18; // rows of the exported table
};

// Observations of one generated week: (customer type, pallets, true minutes) per order.
[[nodiscard]] std::vector<Observation> observations(const WeekInstance& week,
                                                    const std::map<std::string, Minutes>& minutes);

// Quantile levels stored in the model: 0.5, 0.8, 0.9, 0.95.
[[nodiscard]] LearnedModel fit(const std::vector<Observation>& history);

class LearnedEstimator final : public Estimator {
 public:
  explicit LearnedEstimator(LearnedModel model) : model_(std::move(model)) {}
  [[nodiscard]] std::vector<ServiceEstimate> estimate(
      std::span<const Order> orders, std::span<const Customer> customers) const override;
  [[nodiscard]] const LearnedModel& model() const noexcept { return model_; }

 private:
  LearnedModel model_;
};

// JSON with the per-type parameters and the exported table (type x pallets: mu, sigma, quantiles).
[[nodiscard]] nlohmann::json to_json(const LearnedModel& model);
[[nodiscard]] LearnedModel model_from_json(const nlohmann::json& j);

}  // namespace legalvrp::estimate
