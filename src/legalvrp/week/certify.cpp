#include "legalvrp/week/certify.hpp"

#include <algorithm>

#include "legalvrp/domain/day.hpp"
#include "legalvrp/heuristics/territory.hpp"

namespace legalvrp::week {

double BaselineWeek::max_postponed_share() const {
  return postponed_share.empty() ? 0.0 : std::ranges::max(postponed_share);
}

BaselineWeek run_baseline_week(const WeekInstance& week) {
  BaselineWeek out;
  std::vector<DriverWeekState> states;
  for (const auto& d : week.drivers) states.push_back({d.id, 0, 0, std::nullopt});
  std::vector<Order> carried;

  for (int d = 0; d < week.days; ++d) {
    const DayInstance day = make_day_instance(week, d, carried, states);
    DayPlan plan = heuristics::territory_baseline(day);
    const auto dc = check::check_day(day, plan);

    for (const auto& f : dc.drivers) {  // state from the checker's recomputed facts
      if (!f.used) continue;
      auto& st = *std::ranges::find(states, f.driver_id, &DriverWeekState::driver_id);
      st.service_minutes_week += f.service_minutes;
      st.driving_minutes_week += f.driving_minutes;
      st.last_duty_end = d * 24 * 60 + f.duty_end;
    }
    carried.clear();
    for (const auto& id : plan.postponed_order_ids) {
      Order o = *std::ranges::find(week.orders, id, &Order::id);  // original order
      o.postponed_from = o.postponed_from.value_or(o.day);
      carried.push_back(std::move(o));
    }
    out.postponed_share.push_back(day.orders.empty() ? 0.0
                                                     : static_cast<double>(plan.postponed_order_ids.size()) /
                                                           static_cast<double>(day.orders.size()));
    out.plans.push_back(std::move(plan));
  }
  out.check = check::check_week(week, out.plans);
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
