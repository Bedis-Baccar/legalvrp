#pragma once
// legalvrp::cg — relaxed pricing for column generation lower bounds (V1-T9, D-118).
//
// For a VALID lower bound it is enough to price exactly a RELAXATION of the legal routes: a set
// of "relaxed routes" containing every route the planners may produce, at a cost never above the
// true one. Relaxed routes of driver k are explored by forward labelling over the stops, with the
// planners' break patterns (none, 45, a lone 30, 15 then 30) as decisions after each service:
//   * ng-routes (Baldacci, Mingozzi & Roberti 2011): a stop is revisited only after leaving its
//     ng-neighbourhood (ng = n: elementary);
//   * capacity, tail-lift; driving since the last qualifying break <= DB at every stop and at the
//     return (breaks are taken at stops), total driving <= DD and the weekly driving left;
//   * times are OPTIMISTIC: earliest service everywhere, each break as early as possible;
//   * work stretches use the LATEST possible stretch start: a break may start as late as
//     min(stretch start + WB, l_i + s_i) (waiting before the break, D-007), and must end in time
//     to reach the next stop's window (or the depot by F_k - R): earliest ready - latest start <= WB;
//   * theta = tE + R - S_k - (break minutes of the pattern) <= min(DS, H^max - W_k), and <= 9 h
//     for a lone 30-min break; tE + R <= F_k;
//   * cost c~ = km cost + c^reg theta + c^ext max(0, W_k + theta - H^thr) + c^fix - idle_k, with the
//     earliest theta: every legal route is a relaxed route and its true theta is not smaller.
// price() returns min over relaxed routes of c~ - sum of duals (<= 0: the empty route), exactly
// (labels within the limit), with dominance on (pattern state, cost, ready time, load, driving,
// driving since the last qualifying break, latest stretch start, ng memory). It also returns the
// best relaxed routes, as candidates for legal columns.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::cg {

inline constexpr std::size_t kMaxPricingOrders = 256;
using NgSet = std::array<std::uint64_t, kMaxPricingOrders / 64>;  // a set of orders (bits)

struct PricingOptions {
  int ng = 8;                       // initial ng-neighbourhood size (>= 1; >= n: elementary routes)
  std::size_t max_labels = 600000;  // above this the run stops and is not exact (then: smaller ng)
  int keep = 30;                    // best relaxed routes returned
  // Past this time the run stops and is not exact.
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};

struct RelaxedRoute {
  std::vector<std::size_t> seq;     // indices into day.orders
  double value = 0.0;               // c~ - sum of duals
};

struct PricingResult {
  bool exact = false;               // the minimum over all relaxed routes (labels within the limit)
  double min_value = 0.0;           // <= 0 (the empty route)
  std::vector<RelaxedRoute> best;   // most negative first
  std::size_t labels = 0;
};

// Relaxed cost data of one driver, shared by every pricing call.
class RelaxedPricer {
 public:
  RelaxedPricer(const DayInstance& day, std::size_t driver);
  // `pi`: dual of each order's cover constraint (indices of day.orders).
  [[nodiscard]] PricingResult price(const std::vector<double>& pi, const PricingOptions& options) const;
  // c~ of a sequence (+ infinity if it is not a relaxed route); for tests.
  [[nodiscard]] double relaxed_cost(const std::vector<std::size_t>& seq) const;

  // ng-neighbourhoods, dynamic (Martinelli, Pecin & Poggi 2014): reset to each order and its ng - 1
  // nearest, or grown along the cycles of a relaxed route so that the route is no longer an
  // ng-route. Any neighbourhoods give a valid relaxation; larger ones a tighter one.
  void reset_ng(int ng);
  bool forbid_cycles(const std::vector<std::size_t>& seq);  // true if a neighbourhood grew
  [[nodiscard]] const std::vector<NgSet>& ng_sets() const noexcept { return neigh_; }
  void set_ng_sets(std::vector<NgSet> sets) { neigh_ = std::move(sets); }
  [[nodiscard]] int ng_max() const;  // size of the largest neighbourhood

 private:
  [[nodiscard]] double work_cost(double theta) const;  // c^reg theta + c^ext max(0, W + theta - thr)

  const DayInstance& day_;
  std::size_t k_;
  std::size_t n_;
  std::vector<bool> compat_;
  std::vector<long long> e_, l_, s_, q_;
  std::vector<std::size_t> node_;
  std::size_t depot_;
  long long S_, F_, P_, R_, DB_, Dmax_, Wmax_, WB_, Q_;
  double c_km_, c_reg_, c_ext_, c_fix_, W0_, thr_, idle_;
  std::vector<long long> sp_back_;  // shortest travel time from each order to the depot
  std::vector<std::vector<std::size_t>> ng_order_;  // other orders by travel time from each order
  std::vector<NgSet> neigh_;         // current ng-neighbourhoods
  const Rules& rules_;
};

}  // namespace legalvrp::cg
