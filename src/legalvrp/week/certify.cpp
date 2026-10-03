#include "legalvrp/week/certify.hpp"

#include <algorithm>

#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/week/loop.hpp"

namespace legalvrp::week {

double BaselineWeek::max_postponed_share() const {
  return postponed_share.empty() ? 0.0 : std::ranges::max(postponed_share);
}

BaselineWeek run_baseline_week(const WeekInstance& week) {
  WeekRun run = run_week(week, heuristics::territory_baseline);
  BaselineWeek out;
  for (std::size_t d = 0; d < run.plans.size(); ++d) {
    const auto orders = run.days[d].orders.size();
    out.postponed_share.push_back(orders == 0 ? 0.0
                                              : static_cast<double>(run.plans[d].postponed_order_ids.size()) /
                                                    static_cast<double>(orders));
  }
  out.plans = std::move(run.plans);
  out.check = std::move(run.week_check);
  return out;
}

bool certifies(const BaselineWeek& b, double max_postponed_share) {
  return b.check.ok() && b.max_postponed_share() <= max_postponed_share + 1e-12;
}

std::optional<CertifiedWeek> generate_certified_week(const data::InstanceConfig& cfg,
                                                     const Config& config, std::uint64_t seed,
                                                     int max_attempts) {
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    data::GeneratedWeek g = data::generate_week(cfg, config, seed, attempt);
    BaselineWeek b = run_baseline_week(g.week);
    if (certifies(b, cfg.max_postponed_share)) {
      g.week.certified = true;
      return CertifiedWeek{std::move(g), std::move(b)};
    }
  }
  return std::nullopt;
}

}  // namespace legalvrp::week
