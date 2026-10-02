#pragma once
// legalvrp::heuristics — exact single-route evaluator (§7, D-007, D-016).
//
// Given a driver and a visiting sequence, returns the cheapest legal schedule over every
// break position ("no break", after order 1, ..., after order m), or the violated rules.
//
// Method. For a fixed sequence and break position, every timing rule is a difference
// constraint x_v - x_u <= c over the times {t0, T_1..T_m, tE}: travel and service, windows,
// shift, the three 6 h work segments, daily and weekly service caps. This is a simple
// temporal network: Bellman-Ford decides feasibility, gives the earliest return, hence the
// minimum temps de service, and an integer schedule achieving it (waiting placed where needed,
// including before the break node, D-007). Driving limits do not depend on times and are
// checked directly. Cost is non-decreasing in temps de service, so min theta = min cost.
//
// Semantics are those of docs/MODEL.md: fixed duty start (D-018), duty = [S_k, tE + R];
// break of BR right after service at the break node; theta = tE + R - S_k - BR * [break];
// theta <= daily_service_max. Minimum theta = earliest return. Among schedules with
// the earliest return, the driver leaves the depot as late as possible (less waiting at
// customers) and serves as early as possible after that.
// Used by the heuristics only; the checker (T4) is written independently.

#include <cstddef>
#include <span>
#include <vector>

#include "legalvrp/domain/models.hpp"
#include "legalvrp/domain/violation.hpp"

namespace legalvrp::heuristics {

struct RouteEvaluation {
  bool legal = false;
  Route route;                  // when legal: times filled (departure, arrivals, starts, return)
  Minutes service_minutes = 0;  // theta: temps de service today
  Minutes driving_minutes = 0;
  double km = 0.0;
  Euros cost = 0.0;             // §6.3 for this driver: km + reg*theta + ext*overtime + fixed
  std::vector<Violation> violations;  // when illegal: the most nearly legal break option's
};

class RouteEvaluator {
 public:
  // Keeps a reference to `day`; it must outlive the evaluator.
  explicit RouteEvaluator(const DayInstance& day);

  // `driver`: index into day.drivers; `orders`: indices into day.orders, in visiting order.
  [[nodiscard]] RouteEvaluation evaluate(std::size_t driver,
                                         std::span<const std::size_t> orders) const;

  [[nodiscard]] const DayInstance& day() const noexcept { return day_; }

 private:
  struct DriverData {
    Pallets capacity = 0;
    bool tail_lift = false;
    const Contract* contract = nullptr;
    Minutes week_service = 0;  // W_k
    Minutes week_driving = 0;  // V_k
  };
  struct OrderData {
    std::size_t node = 0;      // matrix index
    Minutes e = 0, l = 0, s = 0;
    Pallets q = 0;
    bool needs_tail_lift = false;
  };

  const DayInstance& day_;
  std::size_t depot_ = 0;
  std::vector<DriverData> drivers_;
  std::vector<OrderData> orders_;
};

}  // namespace legalvrp::heuristics
