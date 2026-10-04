// T4 acceptance: every label of tests/fixtures/duties.json is reproduced exactly
// (the set of violated rule keys equals the expected set; [] = legal).
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <string>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"

using namespace legalvrp;
using nlohmann::json;

namespace {

json load_corpus() {
  std::ifstream in(fixtures_dir() / "duties.json");
  REQUIRE(in);
  return json::parse(in);
}

template <class T>
T get_or(const json& duty, const json& defaults, const char* key) {
  return duty.contains(key) ? duty.at(key).get<T>() : defaults.at(key).get<T>();
}

// One duty -> (DayInstance with one driver, DayPlan with one route).
std::pair<DayInstance, DayPlan> build(const json& duty, const json& defaults, const Config& cfg) {
  DayInstance day;
  day.rules = cfg.rules;
  day.contracts = cfg.contracts;
  day.costs = cfg.costs;
  day.depot = Depot{"depot", 0, 0};
  day.trucks = {Truck{"t1", get_or<int>(duty, defaults, "capacity"),
                      get_or<bool>(duty, defaults, "truck_tail_lift")}};
  day.drivers = {Driver{"k1", "full_time", "t1", get_or<int>(duty, defaults, "shift_start"),
                        get_or<int>(duty, defaults, "shift_end"), {0}}};
  day.states = {DriverWeekState{"k1", get_or<int>(duty, defaults, "week_service"),
                                get_or<int>(duty, defaults, "week_driving"), std::nullopt}};

  const auto& stops = duty.at("stops");
  const auto legs = duty.at("legs").get<std::vector<Minutes>>();
  for (const auto& s : stops) {
    Customer c;
    c.id = s.at("id").get<std::string>();
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

  // Matrix: depot + stops; the route's legs, 999 elsewhere; km = minutes.
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
    REQUIRE(legs.size() == stops.size() + 1);
    for (std::size_t i = 0; i <= stops.size(); ++i) {
      const std::size_t from = i;                                  // node i = stop i-1, 0 = depot
      const std::size_t to = i < stops.size() ? i + 1 : 0;
      m.time_min[from * n + to] = legs[i];
      m.dist_km[from * n + to] = legs[i];
    }
  }

  DayPlan plan;
  Route r;
  r.driver_id = "k1";
  for (const auto& c : day.customers) r.order_ids.push_back(c.id);
  r.departure = duty.at("departure").get<Minutes>();
  r.service_starts = duty.at("starts").get<std::vector<Minutes>>();
  if (duty.contains("break_after")) r.breaks.push_back({duty.at("break_after").get<std::string>(), cfg.rules.break_length});
  if (duty.contains("breaks")) {  // V1-T8: [[order, minutes], ...]
    for (const auto& b : duty.at("breaks")) r.breaks.push_back({b[0].get<std::string>(), b[1].get<Minutes>()});
  }

  // Consistent arrivals / return unless given (harness convenience; the checker recomputes).
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
  r.return_time = duty.contains("return")  ? duty.at("return").get<Minutes>()
                  : r.order_ids.empty()    ? r.departure
                                           : leave + legs.back();
  plan.routes = {r};
  return {day, plan};
}

}  // namespace

TEST_CASE("duty corpus: at least 30 labelled duties, legal and illegal", "[checker][corpus]") {
  const json corpus = load_corpus();
  const auto& duties = corpus.at("duties");
  CHECK(duties.size() >= 30);
  int legal = 0;
  for (const auto& d : duties) legal += d.at("expect").empty() ? 1 : 0;
  CHECK(legal >= 10);
  CHECK(static_cast<int>(duties.size()) - legal >= 20);
}

TEST_CASE("duty corpus: every label is reproduced exactly", "[checker][corpus]") {
  const Config cfg = load_config();
  const json corpus = load_corpus();
  for (const auto& duty : corpus.at("duties")) {
    const auto name = duty.at("name").get<std::string>();
    INFO("duty: " << name);
    const auto [day, plan] = build(duty, corpus.at("defaults"), cfg);
    const auto result = check::check_day(day, plan);

    std::set<std::string> got;
    std::string detail;
    for (const auto& v : result.violations) {
      got.insert(v.rule);
      detail += v.rule + "(" + v.order_id + ", " + std::to_string(v.amount) + ") ";
    }
    const auto want = duty.at("expect").get<std::set<std::string>>();
    INFO("violations: " << detail);
    CHECK(got == want);
  }
}

TEST_CASE("duty corpus: at least 15 labelled duties for the V1-T8 break rules", "[checker][corpus]") {
  const json corpus = load_corpus();
  int v1 = 0, legal = 0;
  for (const auto& d : corpus.at("duties")) {
    if (d.at("name").get<std::string>().rfind("V1-T8", 0) != 0) continue;
    ++v1;
    legal += d.at("expect").empty() ? 1 : 0;
  }
  CHECK(v1 >= 15);
  CHECK(legal >= 5);
  CHECK(v1 - legal >= 10);
}
