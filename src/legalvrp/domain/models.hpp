#pragma once
// legalvrp::domain — the records of PROJECT_BRIEF §4.
//
// Conventions
//   * Minutes and pallets are integers everywhere in instance data (§0.8).
//   * Times of day are minutes from midnight; day 0 = Monday.
//   * Money is double, in euros.
//   * Records are plain aggregates. They are built once (by the generator or the
//     JSON reader) and then passed by const reference: "immutable by use".

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace legalvrp {

using Minutes = int;
using Pallets = int;
using Euros = double;

enum class CustomerType { grocery, restaurant, site };

struct Customer {
  std::string id;
  std::string name;
  CustomerType type = CustomerType::grocery;
  double x_km = 0.0;               // synthetic geography (mode A)
  double y_km = 0.0;
  std::optional<double> lat;       // real geography (mode B)
  std::optional<double> lon;
  Minutes window_start = 0;        // earliest service start
  Minutes window_end = 0;          // latest service start
  bool needs_tail_lift = false;    // no access class: any truck serves any customer (D-008)
};

struct Order {
  std::string id;
  std::string customer_id;
  int day = 0;                         // 0 = Monday
  Pallets pallets = 0;
  Minutes service_mu = 0;              // >= 1 (subtour elimination relies on it, §6.4)
  Minutes service_sigma = 0;           // always 0 in V0 (§9)
  std::optional<int> postponed_from;   // day it was first due, if carried over
  Euros postpone_penalty = 0.0;
};

struct Truck {
  std::string id;
  Pallets capacity_pallets = 0;
  bool has_tail_lift = false;
};

struct Driver {
  std::string id;
  std::string contract_class;          // key into Contracts (config/contracts.yaml)
  std::string truck_id;                // one fixed truck all week (A2)
  Minutes shift_start = 0;             // S_k
  Minutes shift_end_max = 0;           // F_k
  std::vector<int> available_days;     // 0 = Monday
};

struct DriverWeekState {
  std::string driver_id;
  Minutes service_minutes_week = 0;    // W_k
  Minutes driving_minutes_week = 0;    // V_k
  std::optional<Minutes> last_duty_end;  // absolute minutes since Monday 00:00
};

struct Depot {
  std::string id;
  double x_km = 0.0;
  double y_km = 0.0;
};

// Square matrices over node_ids (depot + customers), row-major, asymmetric allowed.
struct Matrix {
  std::vector<std::string> node_ids;
  std::vector<Minutes> time_min;       // size n*n, integer minutes
  std::vector<double> dist_km;         // size n*n

  [[nodiscard]] std::size_t size() const noexcept { return node_ids.size(); }
  [[nodiscard]] Minutes time(std::size_t from, std::size_t to) const {
    return time_min[from * size() + to];
  }
  [[nodiscard]] double dist(std::size_t from, std::size_t to) const {
    return dist_km[from * size() + to];
  }
  // Index of a node id; throws std::out_of_range if absent.
  [[nodiscard]] std::size_t index_of(const std::string& node_id) const;
};

// ---- configuration (config/*.yaml), loaded by rules.hpp -------------------

struct Rules {                          // §3, all integer minutes
  Minutes drive_before_break = 0;       // DB
  Minutes work_before_break = 0;        // WB
  Minutes break_length = 0;             // BR
  Minutes daily_drive_max = 0;          // DD
  Minutes daily_service_max = 0;        // DS
  Minutes weekly_drive_max = 0;         // WD
  Minutes daily_rest_min = 0;
  Minutes depot_prep = 0;               // P
  Minutes depot_close = 0;              // R
  // Daytime duty window (D-020): no night work by construction.
  Minutes earliest_duty_start = 0;      // every shift_start >= this (05:00)
  Minutes latest_duty_end = 0;          // every shift_end_max <= this (19:00)
};

struct Contract {
  std::string name;
  Minutes weekly_threshold = 0;         // H^thr: extra pay above
  Minutes weekly_service_max = 0;       // H^max
  Euros cost_per_min_regular = 0.0;     // c^reg (tie-breaker for salaried drivers)
  Euros cost_per_min_extra = 0.0;       // c^ext
  Euros fixed_cost_if_used = 0.0;       // c^fix
};

using Contracts = std::vector<Contract>;  // sorted by name

struct Costs {
  Euros cost_per_km = 0.0;              // c^km
  Euros postpone_penalty = 0.0;         // first postponement (base p_i)
  double postpone_escalation = 1.0;     // multiplier per extra day carried (D-019)
  Euros unserved_end_penalty = 0.0;     // minimum penalty on the horizon's last day
};

// ---- one day's problem and its solution ----------------------------------

struct DayInstance {
  int day = 0;
  Depot depot;
  std::vector<Customer> customers;
  std::vector<Order> orders;              // includes orders carried from yesterday
  std::vector<Driver> drivers;            // drivers working that day
  std::vector<Truck> trucks;
  std::vector<DriverWeekState> states;    // one per driver
  Matrix matrix;
  Rules rules;
  Contracts contracts;
  Costs costs;

  // Lookups by id; throw std::out_of_range naming the missing id.
  [[nodiscard]] const Customer& customer(const std::string& id) const;
  [[nodiscard]] const Truck& truck(const std::string& id) const;
  [[nodiscard]] const Contract& contract(const std::string& name) const;
  [[nodiscard]] const DriverWeekState& state(const std::string& driver_id) const;
};

// A stored week (data/instances/<name>/<seed>/): everything needed to build each
// DayInstance. Rules, contracts and costs are embedded so an instance stays
// reproducible even if config/ changes later.
struct WeekInstance {
  std::string name;
  std::uint64_t seed = 0;
  int days = 0;
  Depot depot;
  std::vector<Customer> customers;
  std::vector<Order> orders;               // all days; Order::day says which
  std::vector<Driver> drivers;
  std::vector<Truck> trucks;
  Matrix matrix;                           // stored in matrix.json
  Rules rules;
  Contracts contracts;
  Costs costs;
  bool certified = false;                  // baseline + checker certification (T5)
};

struct Route {
  std::string driver_id;
  std::vector<std::string> order_ids;               // visiting sequence
  std::optional<std::string> break_after_order_id;  // the break follows this service
  Minutes departure = 0;                            // t0 (leaves depot)
  std::vector<Minutes> arrivals;                    // one per order
  std::vector<Minutes> service_starts;              // one per order; may exceed arrival (waiting)
  Minutes return_time = 0;                          // tE (back at depot)
};

// Solver statistics recorded for every solve (§6.7).
struct SolverStats {
  std::string status;                // e.g. OPTIMAL, TIME_LIMIT, BASELINE_FALLBACK
  double runtime_s = 0.0;
  double objective = 0.0;
  double best_bound = 0.0;
  double gap = 0.0;
  double node_count = 0.0;
  int num_vars = 0, num_constrs = 0, num_nz = 0;                       // before presolve
  int presolved_vars = 0, presolved_constrs = 0, presolved_nz = 0;     // after presolve
  bool baseline_fallback = false;    // time limit hit without incumbent
};

struct DayPlan {
  int day = 0;
  std::vector<Route> routes;
  std::vector<std::string> postponed_order_ids;
  Euros objective = 0.0;
  SolverStats solver_stats;
};

}  // namespace legalvrp
