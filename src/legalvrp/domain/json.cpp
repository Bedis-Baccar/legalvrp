#include "legalvrp/domain/json.hpp"

#include <stdexcept>

namespace legalvrp {

using nlohmann::json;

namespace {
template <class T>
void put_opt(json& j, const char* key, const std::optional<T>& v) {
  if (v) j[key] = *v;
}
template <class T>
void get_opt(const json& j, const char* key, std::optional<T>& v) {
  if (const auto it = j.find(key); it != j.end() && !it->is_null()) {
    v = it->template get<T>();
  } else {
    v.reset();
  }
}
}  // namespace

std::string to_string(CustomerType t) {
  switch (t) {
    case CustomerType::grocery: return "grocery";
    case CustomerType::restaurant: return "restaurant";
    case CustomerType::site: return "site";
  }
  throw std::invalid_argument("invalid CustomerType");
}

CustomerType customer_type_from_string(const std::string& s) {
  if (s == "grocery") return CustomerType::grocery;
  if (s == "restaurant") return CustomerType::restaurant;
  if (s == "site") return CustomerType::site;
  throw std::invalid_argument("unknown customer type '" + s + "' (grocery|restaurant|site)");
}

// ---- Customer
void to_json(json& j, const Customer& x) {
  j = json{{"id", x.id},
           {"name", x.name},
           {"type", to_string(x.type)},
           {"x_km", x.x_km},
           {"y_km", x.y_km},
           {"window_start", x.window_start},
           {"window_end", x.window_end},
           {"needs_tail_lift", x.needs_tail_lift}};
  put_opt(j, "lat", x.lat);
  put_opt(j, "lon", x.lon);
}
void from_json(const json& j, Customer& x) {
  j.at("id").get_to(x.id);
  j.at("name").get_to(x.name);
  x.type = customer_type_from_string(j.at("type").get<std::string>());
  j.at("x_km").get_to(x.x_km);
  j.at("y_km").get_to(x.y_km);
  j.at("window_start").get_to(x.window_start);
  j.at("window_end").get_to(x.window_end);
  j.at("needs_tail_lift").get_to(x.needs_tail_lift);
  get_opt(j, "lat", x.lat);
  get_opt(j, "lon", x.lon);
}

// ---- Order
void to_json(json& j, const Order& x) {
  j = json{{"id", x.id},
           {"customer_id", x.customer_id},
           {"day", x.day},
           {"pallets", x.pallets},
           {"service_mu", x.service_mu},
           {"service_sigma", x.service_sigma},
           {"postpone_penalty", x.postpone_penalty}};
  put_opt(j, "postponed_from", x.postponed_from);
}
void from_json(const json& j, Order& x) {
  j.at("id").get_to(x.id);
  j.at("customer_id").get_to(x.customer_id);
  j.at("day").get_to(x.day);
  j.at("pallets").get_to(x.pallets);
  j.at("service_mu").get_to(x.service_mu);
  j.at("service_sigma").get_to(x.service_sigma);
  j.at("postpone_penalty").get_to(x.postpone_penalty);
  get_opt(j, "postponed_from", x.postponed_from);
}

// ---- Truck, Driver, DriverWeekState, Depot
void to_json(json& j, const Truck& x) {
  j = json{{"id", x.id}, {"capacity_pallets", x.capacity_pallets}, {"has_tail_lift", x.has_tail_lift}};
}
void from_json(const json& j, Truck& x) {
  j.at("id").get_to(x.id);
  j.at("capacity_pallets").get_to(x.capacity_pallets);
  j.at("has_tail_lift").get_to(x.has_tail_lift);
}

void to_json(json& j, const Driver& x) {
  j = json{{"id", x.id},
           {"contract_class", x.contract_class},
           {"truck_id", x.truck_id},
           {"shift_start", x.shift_start},
           {"shift_end_max", x.shift_end_max},
           {"available_days", x.available_days}};
}
void from_json(const json& j, Driver& x) {
  j.at("id").get_to(x.id);
  j.at("contract_class").get_to(x.contract_class);
  j.at("truck_id").get_to(x.truck_id);
  j.at("shift_start").get_to(x.shift_start);
  j.at("shift_end_max").get_to(x.shift_end_max);
  j.at("available_days").get_to(x.available_days);
}

void to_json(json& j, const DriverWeekState& x) {
  j = json{{"driver_id", x.driver_id},
           {"service_minutes_week", x.service_minutes_week},
           {"driving_minutes_week", x.driving_minutes_week}};
  put_opt(j, "last_duty_end", x.last_duty_end);
}
void from_json(const json& j, DriverWeekState& x) {
  j.at("driver_id").get_to(x.driver_id);
  j.at("service_minutes_week").get_to(x.service_minutes_week);
  j.at("driving_minutes_week").get_to(x.driving_minutes_week);
  get_opt(j, "last_duty_end", x.last_duty_end);
}

void to_json(json& j, const Depot& x) { j = json{{"id", x.id}, {"x_km", x.x_km}, {"y_km", x.y_km}}; }
void from_json(const json& j, Depot& x) {
  j.at("id").get_to(x.id);
  j.at("x_km").get_to(x.x_km);
  j.at("y_km").get_to(x.y_km);
}

// ---- Matrix (stored as rows for readability)
void to_json(json& j, const Matrix& x) {
  const std::size_t n = x.size();
  json t = json::array();
  json d = json::array();
  for (std::size_t i = 0; i < n; ++i) {
    json tr = json::array();
    json dr = json::array();
    for (std::size_t k = 0; k < n; ++k) {
      tr.push_back(x.time(i, k));
      dr.push_back(x.dist(i, k));
    }
    t.push_back(std::move(tr));
    d.push_back(std::move(dr));
  }
  j = json{{"node_ids", x.node_ids}, {"time_min", std::move(t)}, {"dist_km", std::move(d)}};
}
void from_json(const json& j, Matrix& x) {
  j.at("node_ids").get_to(x.node_ids);
  const std::size_t n = x.node_ids.size();
  const auto& t = j.at("time_min");
  const auto& d = j.at("dist_km");
  if (t.size() != n || d.size() != n) throw std::invalid_argument("matrix: rows != node count");
  x.time_min.assign(n * n, 0);
  x.dist_km.assign(n * n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    if (t[i].size() != n || d[i].size() != n) {
      throw std::invalid_argument("matrix: row " + std::to_string(i) + " has wrong length");
    }
    for (std::size_t k = 0; k < n; ++k) {
      if (!t[i][k].is_number_integer()) {
        throw std::invalid_argument("matrix: time_min must be integer minutes");
      }
      x.time_min[i * n + k] = t[i][k].get<Minutes>();
      x.dist_km[i * n + k] = d[i][k].get<double>();
    }
  }
}

// ---- Rules, Contract, Costs
void to_json(json& j, const Rules& x) {
  j = json{{"drive_before_break", x.drive_before_break}, {"work_before_break", x.work_before_break},
           {"break_length", x.break_length},             {"daily_drive_max", x.daily_drive_max},
           {"daily_service_max", x.daily_service_max},   {"weekly_drive_max", x.weekly_drive_max},
           {"daily_rest_min", x.daily_rest_min},         {"depot_prep", x.depot_prep},
           {"depot_close", x.depot_close}};
}
void from_json(const json& j, Rules& x) {
  j.at("drive_before_break").get_to(x.drive_before_break);
  j.at("work_before_break").get_to(x.work_before_break);
  j.at("break_length").get_to(x.break_length);
  j.at("daily_drive_max").get_to(x.daily_drive_max);
  j.at("daily_service_max").get_to(x.daily_service_max);
  j.at("weekly_drive_max").get_to(x.weekly_drive_max);
  j.at("daily_rest_min").get_to(x.daily_rest_min);
  j.at("depot_prep").get_to(x.depot_prep);
  j.at("depot_close").get_to(x.depot_close);
}

void to_json(json& j, const Contract& x) {
  j = json{{"name", x.name},
           {"weekly_threshold", x.weekly_threshold},
           {"weekly_service_max", x.weekly_service_max},
           {"cost_per_min_regular", x.cost_per_min_regular},
           {"cost_per_min_extra", x.cost_per_min_extra},
           {"fixed_cost_if_used", x.fixed_cost_if_used}};
}
void from_json(const json& j, Contract& x) {
  j.at("name").get_to(x.name);
  j.at("weekly_threshold").get_to(x.weekly_threshold);
  j.at("weekly_service_max").get_to(x.weekly_service_max);
  j.at("cost_per_min_regular").get_to(x.cost_per_min_regular);
  j.at("cost_per_min_extra").get_to(x.cost_per_min_extra);
  j.at("fixed_cost_if_used").get_to(x.fixed_cost_if_used);
}

void to_json(json& j, const Costs& x) {
  j = json{{"cost_per_km", x.cost_per_km}, {"postpone_penalty", x.postpone_penalty}};
}
void from_json(const json& j, Costs& x) {
  j.at("cost_per_km").get_to(x.cost_per_km);
  j.at("postpone_penalty").get_to(x.postpone_penalty);
}

// ---- WeekInstance (matrix excluded: matrix.json)
void to_json(json& j, const WeekInstance& x) {
  j = json{{"name", x.name},           {"seed", x.seed},       {"days", x.days},
           {"depot", x.depot},         {"customers", x.customers}, {"orders", x.orders},
           {"drivers", x.drivers},     {"trucks", x.trucks},   {"rules", x.rules},
           {"contracts", x.contracts}, {"costs", x.costs},     {"certified", x.certified}};
}
void from_json(const json& j, WeekInstance& x) {
  j.at("name").get_to(x.name);
  j.at("seed").get_to(x.seed);
  j.at("days").get_to(x.days);
  j.at("depot").get_to(x.depot);
  j.at("customers").get_to(x.customers);
  j.at("orders").get_to(x.orders);
  j.at("drivers").get_to(x.drivers);
  j.at("trucks").get_to(x.trucks);
  j.at("rules").get_to(x.rules);
  j.at("contracts").get_to(x.contracts);
  j.at("costs").get_to(x.costs);
  j.at("certified").get_to(x.certified);
}

// ---- Route, SolverStats, DayPlan
void to_json(json& j, const Route& x) {
  j = json{{"driver_id", x.driver_id},   {"order_ids", x.order_ids},
           {"departure", x.departure},   {"arrivals", x.arrivals},
           {"service_starts", x.service_starts}, {"return_time", x.return_time}};
  put_opt(j, "break_after_order_id", x.break_after_order_id);
}
void from_json(const json& j, Route& x) {
  j.at("driver_id").get_to(x.driver_id);
  j.at("order_ids").get_to(x.order_ids);
  j.at("departure").get_to(x.departure);
  j.at("arrivals").get_to(x.arrivals);
  j.at("service_starts").get_to(x.service_starts);
  j.at("return_time").get_to(x.return_time);
  get_opt(j, "break_after_order_id", x.break_after_order_id);
}

void to_json(json& j, const SolverStats& x) {
  j = json{{"status", x.status},
           {"runtime_s", x.runtime_s},
           {"objective", x.objective},
           {"best_bound", x.best_bound},
           {"gap", x.gap},
           {"node_count", x.node_count},
           {"num_vars", x.num_vars},
           {"num_constrs", x.num_constrs},
           {"num_nz", x.num_nz},
           {"presolved_vars", x.presolved_vars},
           {"presolved_constrs", x.presolved_constrs},
           {"presolved_nz", x.presolved_nz},
           {"baseline_fallback", x.baseline_fallback}};
}
void from_json(const json& j, SolverStats& x) {
  j.at("status").get_to(x.status);
  j.at("runtime_s").get_to(x.runtime_s);
  j.at("objective").get_to(x.objective);
  j.at("best_bound").get_to(x.best_bound);
  j.at("gap").get_to(x.gap);
  j.at("node_count").get_to(x.node_count);
  j.at("num_vars").get_to(x.num_vars);
  j.at("num_constrs").get_to(x.num_constrs);
  j.at("num_nz").get_to(x.num_nz);
  j.at("presolved_vars").get_to(x.presolved_vars);
  j.at("presolved_constrs").get_to(x.presolved_constrs);
  j.at("presolved_nz").get_to(x.presolved_nz);
  j.at("baseline_fallback").get_to(x.baseline_fallback);
}

void to_json(json& j, const DayPlan& x) {
  j = json{{"day", x.day},
           {"routes", x.routes},
           {"postponed_order_ids", x.postponed_order_ids},
           {"objective", x.objective},
           {"solver_stats", x.solver_stats}};
}
void from_json(const json& j, DayPlan& x) {
  j.at("day").get_to(x.day);
  j.at("routes").get_to(x.routes);
  j.at("postponed_order_ids").get_to(x.postponed_order_ids);
  j.at("objective").get_to(x.objective);
  j.at("solver_stats").get_to(x.solver_stats);
}

std::string to_canonical_text(const json& j) { return j.dump(2) + "\n"; }

}  // namespace legalvrp
