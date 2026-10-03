#pragma once
// legalvrp::model — Gurobi wrapper (§6.7): parameters from config/solver.yaml, complete MIP
// start, one log file per solve, .lp on request, IIS (.ilp) on infeasibility, statistics before
// and after presolve, extraction. The plan it returns is NOT yet validated: week/solve_day runs
// the checker and falls back to the baseline when needed.

#include <filesystem>
#include <optional>
#include <string>

#include "gurobi_c++.h"
#include "legalvrp/domain/models.hpp"
#include "legalvrp/model/bigm.hpp"

namespace legalvrp::model {

struct SolverConfig {
  double time_limit = 120.0;  // seconds
  double mip_gap = 0.01;
  int threads = 0;            // 0 = all cores
  int seed = 0;
  int output_flag = 0;
  int mip_focus = 0;          // Gurobi MIPFocus: 0 balanced, 1 feasibility, 2 optimality, 3 bound
  bool write_lp = false;
  bool write_iis_on_infeasible = true;
};

// `default` section, overridden by `profiles.<profile>` if present. ConfigError names the key.
[[nodiscard]] SolverConfig load_solver_config(const std::filesystem::path& solver_yaml,
                                              const std::string& profile);

struct MilpOptions {
  Formulation formulation = Formulation::strong;
  SolverConfig solver;
  std::optional<DayPlan> start;        // legal plan used as a complete MIP start
  std::filesystem::path log_dir;       // empty: no files written
  std::string tag = "day";             // file stem for log / lp / ilp
  bool compute_lp_bound = false;       // also solve the LP relaxation (formulation tightness)
  bool connectivity_cuts = true;       // user cuts separated by max-flow (D-030)
  bool per_driver_cuts = true;         // also per-driver connectivity cuts near the root (D-030)
};

struct MilpResult {
  std::optional<DayPlan> plan;         // incumbent, if any
  SolverStats stats;                   // §6.7 statistics
  bool start_accepted = false;         // start given and representable in the model
  int binaries = 0;
  int arcs = 0, arcs_full = 0;         // after / before pruning
  std::optional<double> lp_bound;      // LP relaxation value (compute_lp_bound)
  long long cuts_added = 0;            // connectivity cuts added during the search
};

// Throws std::runtime_error if the model is infeasible (after writing the IIS when enabled):
// "postpone everything" is always feasible, so infeasibility means inconsistent input.
[[nodiscard]] MilpResult solve_day_milp(GRBEnv& env, const DayInstance& day, const MilpOptions& options);

}  // namespace legalvrp::model
