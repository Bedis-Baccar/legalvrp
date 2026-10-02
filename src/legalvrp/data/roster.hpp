#pragma once
// legalvrp::data — drivers and trucks from the instance config (§5.1). Deterministic.
// Driver k<i> drives truck t<i> all week (A2) and is available every day of the horizon.

#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::data {

struct Roster {
  std::vector<Driver> drivers;
  std::vector<Truck> trucks;
};

// shift_end_max = min(shift_start + shift_span_max, rules.latest_duty_end) (D-020).
[[nodiscard]] Roster make_roster(const InstanceConfig& cfg, const Rules& rules);

}  // namespace legalvrp::data
