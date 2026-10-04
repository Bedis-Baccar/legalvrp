#pragma once
// legalvrp::heuristics — exact single-route evaluator (§7, D-007, D-016).
//
// Given a driver and a visiting sequence, returns the cheapest legal schedule over every break
// pattern and position (BreakPattern: none, one 45-min break, and with the V1-T8 switches a
// lone 30-min break or the 15 + 30 split), or the violated rules of the V0 options.
//
// Method. For a fixed sequence and break pattern, every timing rule is a difference
// constraint x_v - x_u <= c over the times {t0, T_1..T_m, tE}: travel and service, windows,
// shift, the 6 h work stretches, daily and weekly service caps. This is a simple
// temporal network: Bellman-Ford decides feasibility, gives the earliest return, hence the
// minimum temps de service, and an integer schedule achieving it (waiting placed where needed,
// including before a break node, D-007). Driving limits do not depend on times and are
// checked directly. Cost is non-decreasing in temps de service, so min theta = min cost.
//
// Semantics are those of docs/MODEL.md: fixed duty start (D-018), duty = [S_k, tE + R];
// each break right after service at its node; theta = tE + R - S_k - (break minutes);
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

// A break pattern of the planners (docs/MODEL.md, V1-T8): none, one full break (BR), one short
// break (30 min: only when the day's driving needs no break and work stays <= 9 h), or the
// 15 + 30 split. Positions are indices into the visiting sequence.
struct BreakPattern {
  int i = -1, j = -1;      // first and second break position, -1 = none
  Minutes mi = 0, mj = 0;  // their minutes
  bool short_break = false;
  [[nodiscard]] static BreakPattern full(int b, Minutes m) { return {b, -1, m, 0, false}; }
  [[nodiscard]] static BreakPattern short_one(int b, Minutes m) { return {b, -1, m, 0, true}; }
  [[nodiscard]] static BreakPattern split(int a, int b, Minutes m1, Minutes m2) { return {a, b, m1, m2, false}; }
  [[nodiscard]] int count() const noexcept { return i < 0 ? 0 : j < 0 ? 1 : 2; }
  [[nodiscard]] Minutes total() const noexcept { return mi + mj; }
  [[nodiscard]] Minutes minutes_after(int pos) const noexcept { return (pos == i ? mi : 0) + (pos == j ? mj : 0); }
  bool operator==(const BreakPattern&) const = default;
};

struct RouteEvaluation {
  bool legal = false;
  Route route;                  // when legal: times filled (departure, arrivals, starts, return)
  Minutes service_minutes = 0;  // theta: temps de service today
  Minutes driving_minutes = 0;
  double km = 0.0;
  Euros cost = 0.0;             // §6.3 for this driver: km + reg*theta + ext*overtime + fixed
  std::vector<Violation> violations;  // when illegal: the most nearly legal break option's
};

struct QuickEvaluation {
  bool legal = false;
  Euros cost = 0.0;
  Minutes service_minutes = 0;
  Minutes driving_minutes = 0;
  double km = 0.0;
  BreakPattern breaks;   // of the cheapest legal option
};

class RouteEvaluator {
 public:
  // Keeps a reference to `day`; it must outlive the evaluator.
  explicit RouteEvaluator(const DayInstance& day);

  // `driver`: index into day.drivers; `orders`: indices into day.orders, in visiting order.
  [[nodiscard]] RouteEvaluation evaluate(std::size_t driver,
                                         std::span<const std::size_t> orders) const;

  // Fast exact evaluation (V1-T1, D-102): same legality, temps de service, driving, km and cost as
  // evaluate(), in O(n), by composing "earliest ready time" functions f(x) = max(x + d, r) on
  // x <= L (time-window concatenation, Vidal et al. 2013) per break position. No schedule is
  // produced: use evaluate() for the reported route.
  [[nodiscard]] QuickEvaluation quick(std::size_t driver, std::span<const std::size_t> orders) const;

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
