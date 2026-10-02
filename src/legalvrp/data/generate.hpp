#pragma once
// legalvrp::data — instance generation pipeline (§5.1).
//
// Streams: geography = split(1), matrix noise = split(2), orders = split(3) (+ one per day),
// so changing one component never shifts the draws of another.
//
// Certification (§5.1 "Validity": a baseline plan passing the checker with <= 20 %
// postponed) needs T4 + T5; until then WeekInstance::certified stays false (D-011).

#include <cstdint>
#include <string>
#include <vector>

#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/domain/rules.hpp"

namespace legalvrp::data {

struct GeneratedWeek {
  WeekInstance week;
  TrueService truth;  // = service_mu in V0
};

// Throws ConfigError if the instance config is inconsistent with the rules/contracts,
// std::logic_error if the generated week breaks an invariant (a generator bug).
[[nodiscard]] GeneratedWeek generate_week(const InstanceConfig& cfg, const Config& config,
                                          std::uint64_t seed);

// Structural invariants of a stored week; returns one message per problem (empty = valid).
// Ids unique and resolvable; windows inside the day; s_i >= 1; sigma = 0; pallets within
// [1, largest truck]; matrix square over depot + customers, integer minutes, zero diagonal,
// no negative entries; shifts consistent with the daily rest; contracts known.
[[nodiscard]] std::vector<std::string> validate_week(const WeekInstance& week);

}  // namespace legalvrp::data
