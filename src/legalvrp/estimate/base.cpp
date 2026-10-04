#include "legalvrp/estimate/base.hpp"

#include <algorithm>
#include <cmath>

namespace legalvrp::estimate {

void apply(WeekInstance& week, const Estimator& estimator, double z) {
  const auto e = estimator.estimate(week.orders, week.customers);
  for (std::size_t i = 0; i < week.orders.size(); ++i) {
    Order& o = week.orders[i];
    o.service_mu = std::max<Minutes>(1, static_cast<Minutes>(std::floor(e[i].mu + z * e[i].sigma + 0.5)));
    o.service_sigma = static_cast<Minutes>(std::floor(e[i].sigma + 0.5));
  }
}

}  // namespace legalvrp::estimate

namespace legalvrp::estimate {

void reserve_time(WeekInstance& week, Minutes reserve) {
  if (reserve <= 0) return;
  Rules& r = week.rules;
  r.work_before_break = std::max<Minutes>(1, r.work_before_break - reserve);
  r.daily_service_max = std::max<Minutes>(1, r.daily_service_max - reserve);
  r.latest_duty_end -= reserve;
  for (auto& d : week.drivers) d.shift_end_max = std::max(d.shift_start + 1, d.shift_end_max - reserve);
  for (auto& c : week.contracts) c.weekly_service_max = std::max<Minutes>(1, c.weekly_service_max - reserve);
  for (auto& c : week.customers) c.window_end = std::max(c.window_start, c.window_end - reserve);
}

}  // namespace legalvrp::estimate
