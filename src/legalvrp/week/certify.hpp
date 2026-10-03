#pragma once
// legalvrp::week — the baseline played over a week, and instance certification (§5.1 Validity).
//
// run_baseline_week: rolling Monday-Friday with the territory baseline. Each day is built
// with make_day_instance (carried orders, escalating penalties), the weekly state is updated
// from the checker's recomputed facts (never from the plan), and the whole week is checked
// again in week mode at the end.
//
// Certification: an instance is certified if the baseline week has zero checker violations
// and no day postpones more than max_postponed_share of its orders. Otherwise the generator
// is re-run with the next attempt (derived seed) — the brief's "regenerated".

#include <cstdint>
#include <optional>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/domain/rules.hpp"

namespace legalvrp::week {

struct BaselineWeek {
  std::vector<DayPlan> plans;
  check::WeekCheck check;                 // independent week-mode check of `plans`
  std::vector<double> postponed_share;    // per day: postponed / orders of that day
  [[nodiscard]] double max_postponed_share() const;
};

[[nodiscard]] BaselineWeek run_baseline_week(const WeekInstance& week);

[[nodiscard]] bool certifies(const BaselineWeek& b, double max_postponed_share);

struct CertifiedWeek {
  data::GeneratedWeek generated;  // week.certified = true, week.attempt = attempts used - 1
  BaselineWeek baseline;
};

// Tries attempts 0, 1, ... until one certifies; nullopt after max_attempts.
[[nodiscard]] std::optional<CertifiedWeek> generate_certified_week(
    const data::InstanceConfig& cfg, const Config& config, std::uint64_t seed, int max_attempts = 50);

}  // namespace legalvrp::week
