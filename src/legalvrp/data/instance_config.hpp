#pragma once
// legalvrp::data — instance generator parameters (config/instance_<name>.yaml, §5.1).

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

struct CustomerTypeParams {
  CustomerType type = CustomerType::grocery;
  double share = 0.0;            // probability a pool customer has this type
  Minutes window_start = 0;      // service-start window
  Minutes window_end = 0;
  double pallets_lambda = 0.0;   // pallets = 1 + Poisson(lambda), capped at the largest truck
  double tail_lift_prob = 0.0;   // probability the customer needs a tail-lift
  double order_weight = 1.0;     // relative chance of ordering on a given day
  // True service minutes (V1-T6): lognormal, mean = fixed + per_pallet * pallets, cv = sigma / mean.
  // The defaults reproduce the planning estimate exactly (10 + 6 * pallets, no variance, V0).
  double service_fixed = 10.0;
  double service_per_pallet = 6.0;
  double service_cv = 0.0;
};

struct DriverTemplate {
  std::string contract;
  Minutes shift_start = 0;
};

struct TruckTemplate {
  Pallets capacity_pallets = 0;
  bool has_tail_lift = false;
};

struct InstanceConfig {
  std::string name;
  int days = 1;
  int orders_min = 0, orders_max = 0;      // orders per day
  double customer_pool_factor = 2.0;       // pool size = ceil(factor * orders_max)
  int towns_min = 0, towns_max = 0;
  double town_distance_min_km = 0.0, town_distance_max_km = 0.0;
  double spread_min_km = 0.0, spread_max_km = 0.0;
  double detour_factor = 1.3;              // road km = euclid km * detour
  double speed_kmh = 55.0;
  Minutes access_minutes_per_leg = 4;      // parking/manoeuvring, every truck
  double asymmetric_noise = 0.05;          // +-5 % on travel time, per direction
  std::vector<CustomerTypeParams> customer_types;
  std::vector<double> weekday_factor;      // Mon..Fri multipliers of the day's volume
  std::vector<DriverTemplate> drivers;     // driver i drives truck i (A2)
  std::vector<TruckTemplate> trucks;
  Minutes shift_span_max = 765;            // F_k - S_k
  double max_postponed_share = 0.20;       // certification threshold (§5.1 Validity)
};

// Throws ConfigError naming the file and the dotted key.
[[nodiscard]] InstanceConfig parse_instance_config(std::string_view yaml, std::string_view source);
[[nodiscard]] InstanceConfig load_instance_config(const std::filesystem::path& file);

}  // namespace legalvrp::data

namespace legalvrp::data {

// config/instance_scale.yaml (task T9): sizes, seeds and the base instance whose geography,
// customer types, contracts and driver/truck templates are reused.
struct ScaleConfig {
  std::vector<int> orders;            // n per day
  std::vector<std::uint64_t> seeds;
  int drivers_per_orders = 5;         // K = ceil(n / drivers_per_orders)
  std::string solver_profile = "scale";
  std::string name = "scale";         // family name: instances are <name>_n<n>
  int days = 1;                       // horizon of each instance (optional key, default 1)
  InstanceConfig base;
};

[[nodiscard]] ScaleConfig load_scale_config(const std::filesystem::path& file);

// Instance config with exactly n orders per day over `days` days and K = ceil(n / drivers_per_orders)
// drivers, driver and truck templates cycled from the base (used by T9 scale and V1 large).
[[nodiscard]] InstanceConfig scale_instance(const ScaleConfig& s, int n);

}  // namespace legalvrp::data
