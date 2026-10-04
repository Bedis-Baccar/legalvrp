#include "legalvrp/sim/realise.hpp"

#include <algorithm>

#include "legalvrp/data/orders.hpp"

namespace legalvrp::sim {

RealisedDay realise_day(const DayInstance& day, const DayPlan& plan,
                        const std::map<std::string, Minutes>& service_true) {
  RealisedDay out;
  out.day = day;
  for (auto& o : out.day.orders) {
    if (const auto it = service_true.find(o.id); it != service_true.end()) o.service_mu = it->second;
  }
  out.plan = plan;
  const Matrix& m = day.matrix;
  const std::size_t depot = m.index_of(day.depot.id);

  for (std::size_t r = 0; r < out.plan.routes.size(); ++r) {
    const Route& planned = plan.routes[r];
    Route& rt = out.plan.routes[r];
    DutyOutcome duty;
    duty.driver_id = rt.driver_id;
    duty.stops = static_cast<int>(rt.order_ids.size());
    std::size_t here = depot;
    Minutes leave = rt.departure;
    for (std::size_t i = 0; i < rt.order_ids.size(); ++i) {
      const Order& o = *std::ranges::find(out.day.orders, rt.order_ids[i], &Order::id);
      const std::size_t there = m.index_of(o.customer_id);
      const Minutes arrival = leave + m.time(here, there);
      const Minutes start = std::max(arrival, planned.service_starts[i]);
      rt.arrivals[i] = arrival;
      rt.service_starts[i] = start;
      duty.max_delay = std::max(duty.max_delay, start - planned.service_starts[i]);
      leave = start + o.service_mu;
      for (const auto& b : rt.breaks) {
        if (b.after_order_id == o.id) leave += b.minutes;  // the planned break, as planned
      }
      here = there;
    }
    rt.return_time = leave + m.time(here, depot);
    duty.overrun = rt.return_time - planned.return_time;
    out.duties.push_back(std::move(duty));
  }

  out.check = check::check_day(out.day, out.plan);
  for (const auto& v : out.check.violations) {
    const auto d = std::ranges::find(out.duties, v.driver_id, &DutyOutcome::driver_id);
    if (d == out.duties.end()) continue;  // day-level (coverage): cannot come from re-timing
    if (v.rule == rule::time_window) {
      d->late = true;
      ++d->late_stops;
    } else {
      d->illegal = true;
      if (std::ranges::find(d->rules, v.rule) == d->rules.end()) d->rules.push_back(v.rule);
    }
  }
  return out;
}

std::map<std::string, Minutes> sample_truth(const data::TrueService& truth, data::Rng& rng) {
  std::map<std::string, Minutes> out;
  for (const auto& [id, mean] : truth.mean) {
    const auto s = truth.sigma.find(id);
    const double cv = s != truth.sigma.end() && mean > 0.0 ? s->second / mean : 0.0;
    out[id] = data::sample_service(mean, cv, rng);
  }
  return out;
}

}  // namespace legalvrp::sim
