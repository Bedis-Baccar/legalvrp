#include "legalvrp/viz/timeline.hpp"

#include <algorithm>
#include <stdexcept>

namespace legalvrp::viz {

namespace {
const Order& find_order(const DayInstance& day, const std::string& id) {
  const auto it = std::ranges::find(day.orders, id, &Order::id);
  if (it == day.orders.end()) throw std::out_of_range("viz: unknown order " + id);
  return *it;
}
const Driver& find_driver(const DayInstance& day, const std::string& id) {
  const auto it = std::ranges::find(day.drivers, id, &Driver::id);
  if (it == day.drivers.end()) throw std::out_of_range("viz: unknown driver " + id);
  return *it;
}
}  // namespace

Timeline build_timeline(const DayInstance& day, const Route& route) {
  Timeline t;
  t.driver_id = route.driver_id;
  if (route.order_ids.empty()) return t;
  const Driver& driver = find_driver(day, route.driver_id);
  const Rules& r = day.rules;
  const Matrix& m = day.matrix;

  Minutes drive_since = 0;    // since the last qualifying break
  Minutes work_since = 0;     // since the duty start or the last break
  bool part = false;          // a first split part was taken since the last qualifying break
  auto push = [&](std::string_view kind, Minutes start, Minutes end, const std::string& order) {
    if (end <= start) return;
    const Minutes len = end - start;
    if (kind == segment::rest) {
      t.breaks += len;
      work_since = 0;
      if (len >= r.break_length || (part && len >= r.split_break_second)) {
        drive_since = 0;
        part = false;
      } else {
        part = true;
      }
    } else {
      work_since += len;
      if (kind == segment::drive) {
        drive_since += len;
        t.driving += len;
      }
    }
    t.segments.push_back({std::string(kind), start, end, order, drive_since, work_since});
  };

  t.duty_start = driver.shift_start;
  const Minutes prep_start = std::max(driver.shift_start, route.departure - r.depot_prep);
  push(segment::idle, driver.shift_start, prep_start, "");
  push(segment::prep, prep_start, route.departure, "");

  const std::size_t depot = m.index_of(day.depot.id);
  std::size_t here = depot;
  Minutes leave = route.departure;
  for (std::size_t i = 0; i < route.order_ids.size(); ++i) {
    const Order& o = find_order(day, route.order_ids[i]);
    const std::size_t there = m.index_of(o.customer_id);
    const Minutes arrival = leave + m.time(here, there);
    t.km += m.dist(here, there);
    t.pallets += o.pallets;
    push(segment::drive, leave, arrival, o.id);
    const Minutes start = i < route.service_starts.size() ? std::max(arrival, route.service_starts[i]) : arrival;
    push(segment::wait, arrival, start, o.id);
    push(segment::service, start, start + o.service_mu, o.id);
    leave = start + o.service_mu;
    for (const auto& b : route.breaks) {
      if (b.after_order_id != o.id) continue;
      push(segment::rest, leave, leave + b.minutes, o.id);
      leave += b.minutes;
    }
    here = there;
  }
  const Minutes back = leave + m.time(here, depot);
  t.km += m.dist(here, depot);
  push(segment::drive, leave, back, "");
  push(segment::close, back, back + r.depot_close, "");
  t.duty_end = back + r.depot_close;
  t.work = t.duty_end - t.duty_start - t.breaks;
  return t;
}

}  // namespace legalvrp::viz
