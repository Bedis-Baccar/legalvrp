#pragma once
// legalvrp::alns — Adaptive Large Neighbourhood Search for the daily problem (V1-T2, D-103).
//
// Solution: one stop sequence per driver + a bank of unrouted orders (each costs its
// postponement penalty). Cost = the V0 objective (km, regular time, overtime, fixed, penalties),
// evaluated exactly by RouteEvaluator::quick, so ALNS, MILP and checker agree on every number.
//
// Destroy: random, worst (largest removal saving), related (Shaw: distance, time window, size),
// route (empty one route). Repair: greedy, regret-2, regret-3 insertion with cached best
// positions per (order, driver); an order is inserted only if that is cheaper than postponing it.
// Acceptance: simulated annealing (start: a solution 5 % worse is accepted with probability 1/2;
// geometric cooling over the run). Adaptive operator weights (Ropke & Pisinger 2006: scores
// 33 / 9 / 13, segments of 100 iterations, reaction 0.1). New best solutions are polished by
// intra-route 2-opt and relocate. With an iteration limit (and a generous time limit) the
// search is deterministic for a given seed.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "legalvrp/domain/models.hpp"

namespace legalvrp::alns {

struct Options {
  double time_limit_s = 30.0;
  long long max_iterations = 0;     // 0 = time limit only; > 0 makes the search deterministic
  std::uint64_t seed = 1;
  double start_worse_share = 0.05;  // initial temperature: this relative worsening accepted w.p. 1/2
  double end_temperature_ratio = 1e-3;
  double min_remove_share = 0.10;
  double max_remove_share = 0.40;
  int max_remove = 40;
  int segment = 100;
  double reaction = 0.1;
  bool collect_pool = false;        // keep the routes of accepted solutions (route pool, V1-T3)
  std::size_t max_pool = 50000;
  // Initial routes (one per driver, indices into day.orders); default: territory baseline.
  std::optional<std::vector<std::vector<std::size_t>>> start;
};

struct Column {
  std::size_t driver = 0;
  std::vector<std::size_t> seq;
  Euros cost = 0.0;
};

struct TracePoint {
  double seconds = 0.0;
  Euros best = 0.0;
};

struct Result {
  std::vector<std::vector<std::size_t>> routes;  // per driver
  std::vector<std::size_t> bank;                 // postponed orders
  Euros cost = 0.0;
  DayPlan plan;                                  // schedules from the exact STN evaluator
  long long iterations = 0;
  double runtime_s = 0.0;
  std::vector<TracePoint> trace;                 // every new best
  std::vector<Column> pool;
  std::vector<std::string> operators;            // destroy then repair operators
  std::vector<double> weights;
  std::vector<long long> uses;
};

[[nodiscard]] Result solve(const DayInstance& day, const Options& options = {});

// Plan from sequences (schedules by RouteEvaluator::evaluate); routes must be legal.
[[nodiscard]] DayPlan make_plan(const DayInstance& day, const std::vector<std::vector<std::size_t>>& routes,
                                const std::vector<std::size_t>& bank);

}  // namespace legalvrp::alns
