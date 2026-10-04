#include "legalvrp/check/checker.hpp"

#include <algorithm>
#include <map>
#include <string_view>

#include "legalvrp/domain/day.hpp"

namespace legalvrp::check {

namespace {

constexpr Minutes kDay = 24 * 60;

class Report {
 public:
  explicit Report(std::vector<Violation>& out) : out_(out) {}
  void add(std::string_view rule, const std::string& driver, const std::string& order,
           double amount = 0.0) {
    out_.push_back(Violation{std::string{rule}, driver, order, amount, -1});
  }

 private:
  std::vector<Violation>& out_;
};

// Checks one driver's route and fills `facts`. A route too malformed to walk (sizes, unknown
// ids) gets only its structural violation; its facts stay "unused".
void check_route(const DayInstance& day, const Driver& driver, const DriverWeekState& state,
                 const Route& route, DriverFacts& facts, Report& report) {
  const Rules& r = day.rules;
  const Matrix& m = day.matrix;
  const Truck& truck = day.truck(driver.truck_id);
  const Contract& contract = day.contract(driver.contract_class);
  const std::string& k = driver.id;
  const std::size_t n = route.order_ids.size();

  if (n == 0) return;  // idle driver: nothing to check, no hours consumed
  if (route.arrivals.size() != n || route.service_starts.size() != n) {
    report.add(rule::consistency, k, "", 0);  // arrivals / service_starts must match the sequence
    return;
  }
  // Breaks (V1-T8): each after an order of the route, in route order, at most one per order, and
  // at least split_break_first (15) minutes long; break_at[i] = minutes after the i-th service.
  std::vector<Minutes> break_at(n, 0);
  {
    std::size_t last = 0;
    bool first = true;
    for (const auto& b : route.breaks) {
      const auto it = std::ranges::find(route.order_ids, b.after_order_id);
      const auto pos = static_cast<std::size_t>(it - route.order_ids.begin());
      if (it == route.order_ids.end() || (!first && pos <= last) || b.minutes < r.split_break_first) {
        report.add(rule::consistency, k, b.after_order_id, b.minutes);  // break off route, out of order, too short
        return;
      }
      break_at[pos] = b.minutes;
      last = pos;
      first = false;
    }
  }

  // Resolve orders; unknown ids are reported by the coverage check.
  std::vector<const Order*> orders;
  for (const auto& id : route.order_ids) {
    const auto it = std::ranges::find(day.orders, id, &Order::id);
    if (it == day.orders.end()) return;
    orders.push_back(&*it);
  }

  // Equipment and load.
  Pallets load = 0;
  for (const Order* o : orders) {
    load += o->pallets;
    if (day.customer(o->customer_id).needs_tail_lift && !truck.has_tail_lift) {
      report.add(rule::tail_lift, k, o->id);
    }
  }
  if (load > truck.capacity_pallets) report.add(rule::capacity, k, "", load - truck.capacity_pallets);

  // Shift and daytime window.
  if (route.departure < driver.shift_start + r.depot_prep) {
    report.add(rule::shift, k, "", driver.shift_start + r.depot_prep - route.departure);
  }
  if (driver.shift_start < r.earliest_duty_start) {
    report.add(rule::shift, k, "", r.earliest_duty_start - driver.shift_start);
  }

  // Walk the route as the driver executes it.
  const std::size_t depot = m.index_of(day.depot.id);
  std::size_t here = depot;
  Minutes leave = route.departure;     // when the driver leaves the current place
  Minutes drive = 0;
  double km = 0.0;
  struct Taken {
    std::string order_id;
    Minutes start, minutes, drive;  // break start, length, driving done before it
  };
  std::vector<Taken> taken;
  for (std::size_t i = 0; i < n; ++i) {
    const Order& o = *orders[i];
    const Customer& c = day.customer(o.customer_id);
    const std::size_t there = m.index_of(c.id);
    drive += m.time(here, there);
    km += m.dist(here, there);
    const Minutes arrival = leave + m.time(here, there);
    if (route.arrivals[i] != arrival) {
      report.add(rule::consistency, k, o.id, route.arrivals[i] - arrival);
    }
    const Minutes start = route.service_starts[i];
    if (start < arrival) report.add(rule::consistency, k, o.id, arrival - start);
    if (start < c.window_start) report.add(rule::time_window, k, o.id, c.window_start - start);
    if (start > c.window_end) report.add(rule::time_window, k, o.id, start - c.window_end);

    leave = start + o.service_mu;
    if (break_at[i] > 0) {  // the break follows service: no driving in between
      taken.push_back({o.id, leave, break_at[i], drive});
      leave += break_at[i];
    }
    here = there;
  }
  drive += m.time(here, depot);
  km += m.dist(here, depot);
  const Minutes back = leave + m.time(here, depot);
  if (route.return_time != back) report.add(rule::consistency, k, "", route.return_time - back);

  const Minutes duty_start = driver.shift_start;   // fixed start (D-018)
  const Minutes duty_end = back + r.depot_close;
  if (duty_end > driver.shift_end_max) report.add(rule::shift, k, "", duty_end - driver.shift_end_max);
  if (duty_end > r.latest_duty_end) report.add(rule::shift, k, "", duty_end - r.latest_duty_end);

  // Driving (561/2006 art. 7): at most DB between qualifying breaks. A break qualifies if it lasts
  // >= BR, or >= split_break_second after a part >= split_break_first since the last qualifying one.
  {
    Minutes since = 0;          // driving at the last qualifying break
    bool part = false;          // a first part was taken since then
    int qualifying = 0;
    std::string at;             // order of the last qualifying break
    for (const auto& t : taken) {
      if (t.minutes >= r.break_length || (part && t.minutes >= r.split_break_second)) {
        const Minutes period = t.drive - since;
        if (period > r.drive_before_break) {
          report.add(qualifying == 0 ? rule::drive_before_break : rule::drive_between_breaks, k, t.order_id,
                     period - r.drive_before_break);
        }
        since = t.drive;
        part = false;
        at = t.order_id;
        ++qualifying;
      } else {
        part = true;            // every counted break is >= split_break_first
      }
    }
    if (drive - since > r.drive_before_break) {
      report.add(qualifying == 0 ? rule::drive_without_break : rule::drive_after_break, k, at,
                 drive - since - r.drive_before_break);
    }
  }
  if (drive > r.daily_drive_max) report.add(rule::daily_drive_max, k, "", drive - r.daily_drive_max);

  // Work (2002/15/EC art. 5; everything on duty except the breaks, waiting included): stretches
  // between the duty start, the breaks and the duty end are <= WB.
  Minutes breaks = 0;
  for (std::size_t b = 0; b <= taken.size(); ++b) {
    const Minutes from = b == 0 ? duty_start : taken[b - 1].start + taken[b - 1].minutes;
    const Minutes to = b < taken.size() ? taken[b].start : duty_end;
    if (to - from > r.work_before_break) {
      const std::string_view key = taken.empty()          ? rule::work_without_break
                                   : b == 0               ? rule::work_before_break
                                   : b == taken.size()    ? rule::work_after_break
                                                          : rule::work_between_breaks;
      report.add(key, k, b < taken.size() ? taken[b].order_id : taken.empty() ? "" : taken.back().order_id,
                 to - from - r.work_before_break);
    }
    if (b < taken.size()) breaks += taken[b].minutes;
  }

  // Service time and the week.
  const Minutes service = duty_end - duty_start - breaks;
  // Total breaks (W2): >= short_break_length above WB of work, >= BR above short_break_work_max.
  if (!taken.empty()) {
    const Minutes needed = service > r.short_break_work_max ? r.break_length
                           : service > r.work_before_break  ? r.short_break_length
                                                            : 0;
    if (breaks < needed) report.add(rule::break_too_short, k, taken.front().order_id, needed - breaks);
  }
  if (service > r.daily_service_max) {
    report.add(rule::daily_service_max, k, "", service - r.daily_service_max);
  }
  if (state.service_minutes_week + service > contract.weekly_service_max) {
    report.add(rule::weekly_service_max, k, "",
               state.service_minutes_week + service - contract.weekly_service_max);
  }
  if (state.driving_minutes_week + drive > r.weekly_drive_max) {
    report.add(rule::weekly_drive_max, k, "", state.driving_minutes_week + drive - r.weekly_drive_max);
  }

  facts.used = true;
  facts.service_minutes = service;
  facts.driving_minutes = drive;
  facts.km = km;
  facts.duty_start = duty_start;
  facts.duty_end = duty_end;
}

}  // namespace

DayCheck check_day(const DayInstance& day, const DayPlan& plan) {
  DayCheck out;
  Report report(out.violations);

  // ---- routes belong to known drivers, at most one each
  std::map<std::string, const Route*> route_of;
  for (const auto& rt : plan.routes) {
    const bool known = std::ranges::find(day.drivers, rt.driver_id, &Driver::id) != day.drivers.end();
    if (!known || !route_of.emplace(rt.driver_id, &rt).second) {
      report.add(rule::consistency, rt.driver_id, "", 0);
    }
  }

  // ---- coverage: every order exactly once (served or postponed), no unknown ids
  std::map<std::string, int> seen;
  for (const auto& rt : plan.routes) {
    for (const auto& id : rt.order_ids) ++seen[id];
  }
  for (const auto& id : plan.postponed_order_ids) ++seen[id];
  for (const auto& o : day.orders) {
    const int times = seen.contains(o.id) ? seen[o.id] : 0;
    if (times != 1) report.add(rule::coverage, "", o.id, times);
  }
  for (const auto& [id, times] : seen) {
    if (std::ranges::find(day.orders, id, &Order::id) == day.orders.end()) {
      report.add(rule::coverage, "", id, times);
    }
  }

  // ---- each driver of the day
  for (std::size_t k = 0; k < day.drivers.size(); ++k) {
    const Driver& drv = day.drivers[k];
    const DriverWeekState& st = day.state(drv.id);
    DriverFacts f;
    f.driver_id = drv.id;
    if (const auto it = route_of.find(drv.id); it != route_of.end()) {
      check_route(day, drv, st, *it->second, f, report);
    }
    const Contract& c = day.contract(drv.contract_class);
    const Minutes over = std::max(0, st.service_minutes_week + f.service_minutes - c.weekly_threshold);
    f.cost = day.costs.cost_per_km * f.km + c.cost_per_min_regular * f.service_minutes +
             c.cost_per_min_extra * over + (f.used ? c.fixed_cost_if_used : 0.0);
    out.objective += f.cost;
    out.drivers.push_back(f);
  }

  for (const auto& id : plan.postponed_order_ids) {
    const auto it = std::ranges::find(day.orders, id, &Order::id);
    if (it != day.orders.end()) out.postponement_cost += it->postpone_penalty;
  }
  out.objective += out.postponement_cost;
  return out;
}

WeekCheck check_week(const WeekInstance& week, const std::vector<DayPlan>& plans) {
  WeekCheck out;
  std::vector<DriverWeekState> states;
  for (const auto& d : week.drivers) states.push_back({d.id, 0, 0, std::nullopt});
  std::vector<Order> carried;

  if (static_cast<int>(plans.size()) != week.days) {
    out.violations.push_back({std::string{rule::coverage}, "", "", double(plans.size()), -1});
    return out;
  }

  for (int d = 0; d < week.days; ++d) {
    const DayPlan& plan = plans[static_cast<std::size_t>(d)];
    if (plan.day != d) {
      out.violations.push_back({std::string{rule::consistency}, "", "", double(plan.day), d});
    }
    const DayInstance day = make_day_instance(week, d, carried, states);
    DayCheck dc = check_day(day, plan);
    for (auto v : dc.violations) {
      v.day = d;
      out.violations.push_back(std::move(v));
    }

    // Weekly state from the recomputed facts; daily rest between consecutive duties.
    for (const auto& f : dc.drivers) {
      if (!f.used) continue;
      auto& st = *std::ranges::find(states, f.driver_id, &DriverWeekState::driver_id);
      const Minutes start_abs = d * kDay + f.duty_start;
      if (st.last_duty_end && start_abs - *st.last_duty_end < week.rules.daily_rest_min) {
        out.violations.push_back({std::string{rule::daily_rest_min}, f.driver_id, "",
                                  double(week.rules.daily_rest_min - (start_abs - *st.last_duty_end)), d});
      }
      st.service_minutes_week += f.service_minutes;
      st.driving_minutes_week += f.driving_minutes;
      st.last_duty_end = d * kDay + f.duty_end;
    }

    // Postponed orders go to tomorrow as the ORIGINAL orders (base penalty, first due day).
    carried.clear();
    for (const auto& id : plan.postponed_order_ids) {
      const auto it = std::ranges::find(week.orders, id, &Order::id);
      if (it == week.orders.end()) continue;  // reported as coverage by check_day
      Order o = *it;
      o.postponed_from = o.postponed_from.value_or(o.day);
      carried.push_back(std::move(o));
    }
    out.days.push_back(std::move(dc));
  }

  for (const auto& o : carried) out.unserved_order_ids.push_back(o.id);
  out.final_states = states;
  return out;
}

}  // namespace legalvrp::check
