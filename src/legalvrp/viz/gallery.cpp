#include "legalvrp/viz/gallery.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/viz/bundle.hpp"
#include "legalvrp/viz/timeline.hpp"

namespace legalvrp::viz {

using nlohmann::json;

namespace {
template <class T>
T get_or(const json& duty, const json& defaults, const char* key) {
  return duty.contains(key) ? duty.at(key).get<T>() : defaults.at(key).get<T>();
}
}  // namespace

std::pair<DayInstance, DayPlan> corpus_duty(const json& duty, const json& defaults, const Config& cfg) {
  DayInstance day;
  day.rules = cfg.rules;
  day.contracts = cfg.contracts;
  day.costs = cfg.costs;
  day.depot = Depot{"depot", 0, 0};
  day.trucks = {Truck{"t1", get_or<int>(duty, defaults, "capacity"), get_or<bool>(duty, defaults, "truck_tail_lift")}};
  day.drivers = {Driver{"k1", "full_time", "t1", get_or<int>(duty, defaults, "shift_start"),
                        get_or<int>(duty, defaults, "shift_end"), {0}}};
  day.states = {DriverWeekState{"k1", get_or<int>(duty, defaults, "week_service"),
                                get_or<int>(duty, defaults, "week_driving"), std::nullopt}};

  const auto& stops = duty.at("stops");
  const auto legs = duty.at("legs").get<std::vector<Minutes>>();
  for (const auto& s : stops) {
    Customer c;
    c.id = s.at("id").get<std::string>();
    c.name = c.id;
    c.window_start = s.at("window")[0].get<Minutes>();
    c.window_end = s.at("window")[1].get<Minutes>();
    c.needs_tail_lift = s.value("tail_lift", false);
    day.customers.push_back(c);
    Order o;
    o.id = c.id;
    o.customer_id = c.id;
    o.pallets = s.value("pallets", 1);
    o.service_mu = s.at("service").get<Minutes>();
    o.postpone_penalty = 200;
    day.orders.push_back(o);
  }

  Matrix& m = day.matrix;
  m.node_ids = {"depot"};
  for (const auto& c : day.customers) m.node_ids.push_back(c.id);
  const std::size_t n = m.node_ids.size();
  m.time_min.assign(n * n, 999);
  m.dist_km.assign(n * n, 999.0);
  for (std::size_t i = 0; i < n; ++i) {
    m.time_min[i * n + i] = 0;
    m.dist_km[i * n + i] = 0.0;
  }
  if (!legs.empty()) {
    if (legs.size() != stops.size() + 1) throw std::invalid_argument("corpus duty: legs must be stops + 1");
    for (std::size_t i = 0; i <= stops.size(); ++i) {
      const std::size_t to = i < stops.size() ? i + 1 : 0;
      m.time_min[i * n + to] = legs[i];
      m.dist_km[i * n + to] = legs[i];
    }
  }

  DayPlan plan;
  Route r;
  r.driver_id = "k1";
  for (const auto& c : day.customers) r.order_ids.push_back(c.id);
  r.departure = duty.at("departure").get<Minutes>();
  r.service_starts = duty.at("starts").get<std::vector<Minutes>>();
  if (duty.contains("break_after")) r.breaks.push_back({duty.at("break_after").get<std::string>(), cfg.rules.break_length});
  if (duty.contains("breaks")) {
    for (const auto& b : duty.at("breaks")) r.breaks.push_back({b[0].get<std::string>(), b[1].get<Minutes>()});
  }
  Minutes leave = r.departure;
  std::vector<Minutes> auto_arrivals;
  for (std::size_t i = 0; i < r.order_ids.size(); ++i) {
    auto_arrivals.push_back(leave + legs[i]);
    leave = r.service_starts[i] + day.orders[i].service_mu;
    for (const auto& b : r.breaks) {
      if (b.after_order_id == r.order_ids[i]) leave += b.minutes;
    }
  }
  r.arrivals = duty.contains("arrivals") ? duty.at("arrivals").get<std::vector<Minutes>>() : auto_arrivals;
  r.return_time = duty.contains("return") ? duty.at("return").get<Minutes>()
                  : r.order_ids.empty()   ? r.departure
                                          : leave + legs.back();
  plan.routes = {r};
  return {day, plan};
}

json duty_gallery(const json& corpus, const Config& config) {
  json items = json::array();
  for (const auto& duty : corpus.at("duties")) {
    const auto [day, plan] = corpus_duty(duty, corpus.at("defaults"), config);
    const auto result = check::check_day(day, plan);
    std::set<std::string> got;
    for (const auto& v : result.violations) got.insert(v.rule);
    const auto want = duty.at("expect").get<std::set<std::string>>();
    json stops = json::array();
    for (std::size_t i = 0; i < day.orders.size(); ++i) {
      const auto& c = day.customers[i];
      stops.push_back({{"id", c.id}, {"window_start", c.window_start}, {"window_end", c.window_end},
                       {"service", day.orders[i].service_mu}, {"pallets", day.orders[i].pallets},
                       {"needs_tail_lift", c.needs_tail_lift}});
    }
    const Driver& drv = day.drivers.front();
    json item = {{"name", duty.at("name")},
                 {"expect", want},
                 {"got", got},
                 {"matches", got == want},
                 {"violations", result.violations},
                 {"stops", stops},
                 {"legs", duty.at("legs")},
                 {"route", plan.routes.front()},
                 {"timeline", nullptr},
                 {"shift", {{"start", drv.shift_start}, {"end_max", drv.shift_end_max}}},
                 {"truck", day.trucks.front()},
                 {"state", day.states.front()}};
    if (!plan.routes.front().order_ids.empty()) {
      item["timeline"] = timeline_json(build_timeline(day, plan.routes.front()));
    }
    items.push_back(std::move(item));
  }
  return {{"schema", kBundleSchema}, {"description", corpus.value("description", "")},
          {"rules", config.rules}, {"items", items}};
}

}  // namespace legalvrp::viz
