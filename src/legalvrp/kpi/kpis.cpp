#include "legalvrp/kpi/kpis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace legalvrp::kpi {

double gini(const std::vector<double>& x) {
  if (x.empty()) return 0.0;
  double sum = 0.0;
  for (const double v : x) sum += v;
  if (sum <= 0.0) return 0.0;
  double diff = 0.0;
  for (const double a : x) {
    for (const double b : x) diff += std::abs(a - b);
  }
  const auto n = static_cast<double>(x.size());
  return diff / (2.0 * n * n * (sum / n));
}

WeekKpis compute_week_kpis(const WeekInstance& week, const std::vector<DayInstance>& days,
                           const std::vector<DayPlan>& plans, const check::WeekCheck& wc,
                           const std::string& full_time_contract) {
  if (days.size() != plans.size() || wc.days.size() != plans.size()) {
    throw std::invalid_argument("compute_week_kpis: days, plans and checks must align");
  }
  WeekKpis k;
  k.instance = week.name;
  k.seed = week.seed;
  for (const auto& d : week.drivers) {
    k.drivers.push_back({d.id, d.contract_class, 0, 0,
                         std::ranges::find(week.contracts, d.contract_class, &Contract::name)->weekly_threshold,
                         0, 0});
  }
  int served_total = 0, on_time = 0;
  for (std::size_t d = 0; d < plans.size(); ++d) {
    const DayInstance& day = days[d];
    const DayPlan& plan = plans[d];
    const check::DayCheck& dc = wc.days[d];
    DayKpis dk;
    dk.day = day.day;
    dk.orders = static_cast<int>(day.orders.size());
    dk.postponed = static_cast<int>(plan.postponed_order_ids.size());
    dk.cost = dc.objective;
    dk.postponement_cost = dc.postponement_cost;
    dk.solver = plan.solver_stats;
    for (const auto& f : dc.drivers) {
      dk.km += f.km;
      dk.drivers_used += f.used ? 1 : 0;
      dk.service_minutes[f.driver_id] = f.service_minutes;
      auto& dw = *std::ranges::find(k.drivers, f.driver_id, &DriverWeek::driver_id);
      dw.service_minutes += f.service_minutes;
      dw.driving_minutes += f.driving_minutes;
      dw.days_worked += f.used ? 1 : 0;
    }
    for (const auto& r : plan.routes) {
      dk.served += static_cast<int>(r.order_ids.size());
      for (std::size_t i = 0; i < r.order_ids.size(); ++i) {  // on-time: start inside the window
        const auto o = std::ranges::find(day.orders, r.order_ids[i], &Order::id);
        if (o == day.orders.end()) continue;
        const Customer& c = day.customer(o->customer_id);
        ++served_total;
        on_time += (r.service_starts[i] >= c.window_start && r.service_starts[i] <= c.window_end) ? 1 : 0;
      }
    }
    k.sum_daily_objectives += dk.cost;
    k.cost_total += dc.postponement_cost;
    for (std::size_t j = 0; j < dc.drivers.size(); ++j) {  // route costs without overtime
      const auto& f = dc.drivers[j];
      const Contract& con = day.contract(day.drivers[j].contract_class);
      k.cost_total += day.costs.cost_per_km * f.km + con.cost_per_min_regular * f.service_minutes +
                      (f.used ? con.fixed_cost_if_used : 0.0);
    }
    k.km += dk.km;
    k.served += dk.served;
    k.postponement_decisions += dk.postponed;
    k.days.push_back(std::move(dk));
  }
  std::vector<double> ft_hours;
  for (auto& dw : k.drivers) {
    dw.extra_minutes = std::max(0, dw.service_minutes - dw.threshold);
    k.cost_total += std::ranges::find(week.contracts, dw.contract, &Contract::name)->cost_per_min_extra * dw.extra_minutes;
    k.extra_minutes_total += dw.extra_minutes;
    if (dw.contract == full_time_contract) ft_hours.push_back(dw.service_minutes / 60.0);
  }
  k.hours_gini = gini(ft_hours);
  k.on_time_rate = served_total == 0 ? 1.0 : static_cast<double>(on_time) / served_total;
  k.unserved_order_ids = wc.unserved_order_ids;
  k.unserved_end = static_cast<int>(wc.unserved_order_ids.size());
  k.week_checker_ok = wc.ok();
  k.violations = static_cast<int>(wc.violations.size());
  return k;
}

}  // namespace legalvrp::kpi
