#include "legalvrp/data/generate.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "legalvrp/data/matrix.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/data/rng.hpp"
#include "legalvrp/data/roster.hpp"
#include "legalvrp/data/synthetic.hpp"

namespace legalvrp::data {

namespace {

constexpr Minutes kDay = 24 * 60;

void check_against_config(const InstanceConfig& cfg, const Config& config,
                          const std::string& source) {
  for (std::size_t i = 0; i < cfg.drivers.size(); ++i) {
    const auto& name = cfg.drivers[i].contract;
    if (std::ranges::find(config.contracts, name, &Contract::name) == config.contracts.end()) {
      throw ConfigError(source, "drivers[" + std::to_string(i) + "].contract",
                        "unknown contract class '" + name + "' (see contracts.yaml)");
    }
  }
  // D-020: daytime duties only.
  for (std::size_t i = 0; i < cfg.drivers.size(); ++i) {
    const Minutes s = cfg.drivers[i].shift_start;
    if (s < config.rules.earliest_duty_start || s >= config.rules.latest_duty_end) {
      throw ConfigError(source, "drivers[" + std::to_string(i) + "].shift_start",
                        "must lie in [earliest_duty_start, latest_duty_end) of rules.yaml");
    }
  }
  // A12: same shift start every day keeps the daily rest automatic iff span <= 24 h - rest.
  if (cfg.shift_span_max > kDay - config.rules.daily_rest_min) {
    throw ConfigError(source, "shift_span_max",
                      "must be <= 1440 - daily_rest_min (" +
                          std::to_string(kDay - config.rules.daily_rest_min) + ")");
  }
}

template <class T, class Proj>
void check_unique(const std::vector<T>& items, Proj id, const char* what,
                  std::vector<std::string>& problems) {
  std::set<std::string> seen;
  for (const auto& x : items) {
    if (!seen.insert(id(x)).second) problems.push_back(std::string{"duplicate "} + what + " id '" + id(x) + "'");
  }
}

}  // namespace

GeneratedWeek generate_week(const InstanceConfig& cfg, const Config& config, std::uint64_t seed) {
  check_against_config(cfg, config, "instance_" + cfg.name + ".yaml");

  const Rng root(seed);
  Rng geo_rng = root.split(1);
  Rng noise_rng = root.split(2);
  const Rng orders_rng = root.split(3);

  Geography geo = make_geography(cfg, geo_rng);
  Roster roster = make_roster(cfg, config.rules);
  const Pallets largest =
      std::ranges::max(roster.trucks, {}, &Truck::capacity_pallets).capacity_pallets;

  GeneratedWeek out;
  WeekInstance& w = out.week;
  w.name = cfg.name;
  w.seed = seed;
  w.days = cfg.days;
  w.depot = geo.depot;
  w.matrix = make_matrix(geo.depot, geo.customers, cfg, noise_rng);
  w.orders = make_orders(cfg, geo.customers, largest, config.costs.postpone_penalty, orders_rng);
  w.customers = std::move(geo.customers);
  w.drivers = std::move(roster.drivers);
  w.trucks = std::move(roster.trucks);
  w.rules = config.rules;
  w.contracts = config.contracts;
  w.costs = config.costs;
  w.certified = false;

  for (const auto& o : w.orders) out.truth[o.id] = o.service_mu;  // truth = estimate in V0

  if (const auto problems = validate_week(w); !problems.empty()) {
    throw std::logic_error("generator produced an invalid week: " + problems.front());
  }
  return out;
}

std::vector<std::string> validate_week(const WeekInstance& w) {
  std::vector<std::string> p;
  check_unique(w.customers, [](const Customer& c) { return c.id; }, "customer", p);
  check_unique(w.orders, [](const Order& o) { return o.id; }, "order", p);
  check_unique(w.drivers, [](const Driver& d) { return d.id; }, "driver", p);
  check_unique(w.trucks, [](const Truck& t) { return t.id; }, "truck", p);

  Pallets largest = 0;
  for (const auto& t : w.trucks) largest = std::max(largest, t.capacity_pallets);

  for (const auto& c : w.customers) {
    if (c.window_start < 0 || c.window_end > kDay || c.window_start > c.window_end) {
      p.push_back("customer " + c.id + ": invalid window");
    }
  }
  for (const auto& o : w.orders) {
    const auto c = std::ranges::find(w.customers, o.customer_id, &Customer::id);
    if (c == w.customers.end()) p.push_back("order " + o.id + ": unknown customer " + o.customer_id);
    if (o.day < 0 || o.day >= w.days) p.push_back("order " + o.id + ": day out of horizon");
    if (o.pallets < 1 || o.pallets > largest) p.push_back("order " + o.id + ": pallets out of [1, largest truck]");
    if (o.service_mu < 1) p.push_back("order " + o.id + ": service_mu must be >= 1");
    if (o.service_sigma != 0) p.push_back("order " + o.id + ": service_sigma must be 0 in V0");
    if (!(o.postpone_penalty > 0.0)) p.push_back("order " + o.id + ": postpone_penalty must be > 0");
  }
  for (const auto& d : w.drivers) {
    if (std::ranges::find(w.trucks, d.truck_id, &Truck::id) == w.trucks.end()) {
      p.push_back("driver " + d.id + ": unknown truck " + d.truck_id);
    }
    if (std::ranges::find(w.contracts, d.contract_class, &Contract::name) == w.contracts.end()) {
      p.push_back("driver " + d.id + ": unknown contract " + d.contract_class);
    }
    if (d.shift_start < 0 || d.shift_start >= d.shift_end_max || d.shift_end_max > kDay) {
      p.push_back("driver " + d.id + ": invalid shift");
    }
    if (d.shift_start < w.rules.earliest_duty_start || d.shift_end_max > w.rules.latest_duty_end) {
      p.push_back("driver " + d.id + ": shift outside the daytime duty window (D-020)");
    }
    if (d.shift_end_max - d.shift_start > kDay - w.rules.daily_rest_min) {
      p.push_back("driver " + d.id + ": shift span breaks the daily rest (A12)");
    }
  }

  // Matrix: depot + customers, in that order.
  const Matrix& m = w.matrix;
  const std::size_t n = m.size();
  if (n != w.customers.size() + 1 || n == 0 || m.node_ids[0] != w.depot.id) {
    p.push_back("matrix: nodes must be the depot followed by every customer");
  } else {
    for (std::size_t i = 0; i < w.customers.size(); ++i) {
      if (m.node_ids[i + 1] != w.customers[i].id) {
        p.push_back("matrix: node " + std::to_string(i + 1) + " is not customer " + w.customers[i].id);
        break;
      }
    }
  }
  if (m.time_min.size() != n * n || m.dist_km.size() != n * n) {
    p.push_back("matrix: not square");
  } else {
    for (std::size_t i = 0; i < n; ++i) {
      if (m.time(i, i) != 0 || m.dist(i, i) != 0.0) p.push_back("matrix: non-zero diagonal");
      for (std::size_t j = 0; j < n; ++j) {
        if (m.time(i, j) < 0 || m.dist(i, j) < 0.0) {
          p.push_back("matrix: negative entry");
          i = n;
          break;
        }
      }
    }
  }
  return p;
}

}  // namespace legalvrp::data
