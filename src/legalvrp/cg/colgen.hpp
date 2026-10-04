#pragma once
// legalvrp::cg — column generation for the daily problem: valid lower bounds (V1-T9, D-118).
//
// Master: LP relaxation of the set-partitioning model of the route pool (V1-T3),
//   min  sum_k idle_k + sum_{k,r} (c_kr - idle_k) lambda_kr + sum_i p_i u_i
//   s.t. sum_{k,r} a_ir lambda_kr + u_i = 1   (pi_i)        sum_r lambda_kr <= 1   (mu_k)
// over LEGAL routes only (exact costs by RouteEvaluator::quick). Columns: the ALNS route pool,
// then pricing heuristics: the best relaxed routes of the exact relaxed pricing (cg/pricing.hpp)
// checked with the exact evaluator (or repaired by dropping one stop), and best insertions into
// the routes of the current LP solution.
//
// Lower bound (Lagrangian; valid after ANY master solve): with its duals pi,
//   LB = sum_k idle_k + sum_i pi_i + sum_k m~_k(pi),
// m~_k = the exact minimum of (relaxed cost - sum of duals) over the driver's relaxed routes
// (<= 0: the empty route). Relaxed routes contain every legal route at a cost never above the
// true one, and pi_i <= p_i at an LP optimum, so (pi, mu = m~) is dual feasible: LB <= LP optimum
// over all legal routes <= the day's optimum. The ng-neighbourhoods are dynamic: the cycles of the
// best relaxed routes are forbidden and the pricing redone, which tightens the bound towards the
// elementary one; a growth that exceeds the label limit is undone, and if even the starting
// neighbourhoods are too large they are halved (weaker, still valid). Column generation itself
// uses a truncated labelling first; the exact one runs when it finds nothing.
// Upper bound: the master over the generated columns as a MIP (incumbent as start).

#include <cstddef>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/cg/pricing.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::cg {

struct Options {
  double time_limit_s = 120.0;       // column generation (the final MIP comes on top)
  double mip_time_s = 30.0;
  PricingOptions pricing;
  int max_new_columns = 300;         // per iteration
  std::size_t heuristic_labels = 20000;  // label limit of the truncated pricing (columns only)
  int ng_rounds = 20;                // dynamic ng: re-pricings per driver and bound (cycles forbidden)
  double bound_time_s = 60.0;        // the final bound when column generation stops on time
  int threads = 1;
  std::vector<alns::Column> initial; // e.g. the ALNS pool
  std::vector<std::vector<std::size_t>> incumbent;  // routes per driver (UB and MIP start)
};

struct TracePoint {
  double seconds = 0.0;
  double lp = 0.0;
  double lb = 0.0;                   // best valid bound so far (-inf before the first)
};

struct Result {
  double lower_bound = 0.0;          // best valid lower bound (bound_valid)
  bool bound_valid = false;
  int ng_used = 0;                   // largest ng-neighbourhood of the best bound (dynamic ng)
  int pricing_groups = 0;            // distinct pricing problems (identical drivers share one)
  double lp_value = 0.0;             // last master LP
  double upper_bound = 0.0;          // best integer: final MIP or incumbent
  std::vector<std::vector<std::size_t>> routes;  // of the upper bound
  std::vector<std::size_t> bank;
  bool converged = false;            // no more legal negative column found
  int iterations = 0;
  std::size_t columns = 0;
  double runtime_s = 0.0;
  std::vector<TracePoint> trace;
};

[[nodiscard]] Result solve(GRBEnv& env, const DayInstance& day, const Options& options);

}  // namespace legalvrp::cg
