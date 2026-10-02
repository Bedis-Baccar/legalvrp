#include "legalvrp/data/instance_config.hpp"

#include <set>
#include <tuple>

#include "legalvrp/domain/detail/yaml_section.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/rules.hpp"

namespace legalvrp::data {

namespace {
constexpr Minutes kDay = 24 * 60;
}

InstanceConfig parse_instance_config(std::string_view yaml, std::string_view source_view) {
  const std::string source{source_view};
  detail::YamlSection s(detail::parse_yaml(std::string{yaml}, source), source, "");
  InstanceConfig c;

  c.name = s.text("name");
  c.days = static_cast<int>(s.integer("days", 1, 7));
  {
    const auto [lo, hi] = s.integer_range("orders_per_day", 1, 1000);
    c.orders_min = static_cast<int>(lo);
    c.orders_max = static_cast<int>(hi);
  }
  c.customer_pool_factor = s.real("customer_pool_factor", 1.0, 100.0);
  {
    const auto [lo, hi] = s.integer_range("towns", 1, 100);
    c.towns_min = static_cast<int>(lo);
    c.towns_max = static_cast<int>(hi);
  }
  std::tie(c.town_distance_min_km, c.town_distance_max_km) =
      s.real_range("town_distance_km", 0.0, 1000.0);
  std::tie(c.spread_min_km, c.spread_max_km) = s.real_range("customer_spread_km", 0.0, 100.0);
  c.detour_factor = s.real("detour_factor", 1.0, 5.0);
  c.speed_kmh = s.real("speed_kmh", 1.0, 200.0);
  c.access_minutes_per_leg = s.minutes("access_minutes_per_leg", 0, 120);
  c.asymmetric_noise = s.real("asymmetric_noise", 0.0, 0.5);

  std::set<CustomerType> seen_types;
  double share_total = 0.0;
  for (auto& [type_name, t] : s.named_sections("customer_types")) {
    CustomerTypeParams p;
    try {
      p.type = customer_type_from_string(type_name);
    } catch (const std::invalid_argument& e) {
      t.fail("", e.what());
    }
    seen_types.insert(p.type);
    p.share = t.real("share", 0.0, 1.0);
    const auto [ws, we] = t.integer_range("window", 0, kDay);
    p.window_start = static_cast<Minutes>(ws);
    p.window_end = static_cast<Minutes>(we);
    p.pallets_lambda = t.real("pallets_lambda", 0.0, 50.0);
    p.tail_lift_prob = t.real("tail_lift_prob", 0.0, 1.0);
    p.order_weight = t.real("order_weight", 0.0, 100.0);
    t.reject_unknown();
    share_total += p.share;
    c.customer_types.push_back(p);
  }
  if (share_total <= 0.0) s.fail("customer_types", "shares must not all be 0");

  c.weekday_factor = s.reals("weekday_factor", 0.0, 10.0);
  if (static_cast<int>(c.weekday_factor.size()) < c.days) {
    s.fail("weekday_factor", "needs at least one factor per day (" + std::to_string(c.days) + ")");
  }

  for (auto& d : s.sections("drivers")) {
    DriverTemplate t;
    t.contract = d.text("contract");
    t.shift_start = d.minutes("shift_start", 0, kDay);
    d.reject_unknown();
    c.drivers.push_back(t);
  }
  for (auto& tk : s.sections("trucks")) {
    TruckTemplate t;
    t.capacity_pallets = static_cast<Pallets>(tk.integer("capacity_pallets", 1, 1000));
    t.has_tail_lift = tk.boolean("has_tail_lift");
    tk.reject_unknown();
    c.trucks.push_back(t);
  }
  if (c.trucks.size() != c.drivers.size()) {
    s.fail("trucks", "need exactly one truck per driver (" + std::to_string(c.drivers.size()) +
                         " drivers, " + std::to_string(c.trucks.size()) + " trucks)");
  }
  c.shift_span_max = s.minutes("shift_span_max", 1, kDay);
  for (std::size_t i = 0; i < c.drivers.size(); ++i) {
    if (c.drivers[i].shift_start + c.shift_span_max > kDay) {
      s.fail("drivers[" + std::to_string(i) + "].shift_start",
             "shift_start + shift_span_max must end before midnight");
    }
  }
  c.max_postponed_share = s.real("max_postponed_share", 0.0, 1.0);
  s.reject_unknown();
  return c;
}

InstanceConfig load_instance_config(const std::filesystem::path& file) {
  const auto src = file.filename().string();
  return parse_instance_config(detail::read_text_file(file, src), src);
}

}  // namespace legalvrp::data
