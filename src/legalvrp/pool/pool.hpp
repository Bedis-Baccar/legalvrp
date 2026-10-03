#pragma once
// legalvrp::pool — route-pool set partitioning (V1-T3, D-104).
//
// ALNS visits many legal routes. Each one is a column (driver k, sequence, exact cost incl.
// regular time, overtime against the driver's weekly state and fixed cost). Routes of different
// drivers are independent (every daily and weekly rule is per route), so any combination of
// columns with at most one per driver and every order at most once is a legal day plan:
//
//   min  sum_c cost_c x_c + sum_k idle_k (1 - sum_{c in k} x_c) + sum_i p_i u_i
//   s.t. sum_{c contains i} x_c + u_i = 1   (each order served once or postponed)
//        sum_{c in k} x_c <= 1             (one route per driver)
//
// The incumbent (the ALNS best) is always in the pool and given as MIP start, so the result is
// never worse. solve_hybrid chains ALNS -> pool -> a short ALNS polish from the pool solution.

#include <cstddef>
#include <string>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/domain/models.hpp"

namespace legalvrp::pool {

struct Options {
  double time_limit_s = 15.0;
  double mip_gap = 0.0;
  int threads = 0;
};

struct Result {
  std::vector<std::vector<std::size_t>> routes;  // per driver
  std::vector<std::size_t> bank;
  Euros cost = 0.0;
  Euros incumbent_cost = 0.0;
  Euros bound = 0.0;            // valid only for the restricted problem (these columns)
  std::string status;
  double runtime_s = 0.0;
  std::size_t columns = 0;
  bool improved = false;
};

// `incumbent`: routes (per driver) and bank of a legal solution; added to the pool.
[[nodiscard]] Result solve_pool(GRBEnv& env, const DayInstance& day, const std::vector<alns::Column>& pool,
                                const std::vector<std::vector<std::size_t>>& incumbent_routes,
                                const Options& options = {});

struct HybridOptions {
  alns::Options alns;           // first ALNS phase (pool collection forced on)
  Options pool;
  double polish_time_s = 5.0;   // ALNS restarted from the pool solution
};

struct HybridResult {
  alns::Result first;           // ALNS phase
  Result pool;                  // set partitioning
  alns::Result polish;          // final ALNS phase
  DayPlan plan;
  Euros cost = 0.0;
  double runtime_s = 0.0;
};

[[nodiscard]] HybridResult solve_hybrid(GRBEnv& env, const DayInstance& day, const HybridOptions& options);

}  // namespace legalvrp::pool
