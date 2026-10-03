#include "legalvrp/model/solve.hpp"

#include <memory>
#include <stdexcept>

#include "legalvrp/domain/detail/yaml_section.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/model/extract.hpp"
#include "legalvrp/model/milp_day.hpp"

namespace legalvrp::model {

namespace {

void read_section(detail::YamlSection& s, SolverConfig& c, bool required) {
  auto num = [&](const char* key, double lo, double hi, double& out) {
    if (required || s.has(key)) out = s.real(key, lo, hi);
  };
  auto integer = [&](const char* key, long long lo, long long hi, int& out) {
    if (required || s.has(key)) out = static_cast<int>(s.integer(key, lo, hi));
  };
  auto flag = [&](const char* key, bool& out) {
    if (required || s.has(key)) out = s.boolean(key);
  };
  num("TimeLimit", 0.0, 1e7, c.time_limit);
  num("MIPGap", 0.0, 1.0, c.mip_gap);
  integer("Threads", 0, 1024, c.threads);
  integer("Seed", 0, 2000000000, c.seed);
  integer("OutputFlag", 0, 1, c.output_flag);
  integer("MIPFocus", 0, 3, c.mip_focus);
  flag("write_lp", c.write_lp);
  flag("write_iis_on_infeasible", c.write_iis_on_infeasible);
  s.reject_unknown();
}

std::string status_name(int status) {
  switch (status) {
    case GRB_OPTIMAL: return "OPTIMAL";
    case GRB_INFEASIBLE: return "INFEASIBLE";
    case GRB_INF_OR_UNBD: return "INF_OR_UNBD";
    case GRB_UNBOUNDED: return "UNBOUNDED";
    case GRB_TIME_LIMIT: return "TIME_LIMIT";
    case GRB_NODE_LIMIT: return "NODE_LIMIT";
    case GRB_SOLUTION_LIMIT: return "SOLUTION_LIMIT";
    case GRB_INTERRUPTED: return "INTERRUPTED";
    case GRB_SUBOPTIMAL: return "SUBOPTIMAL";
    default: return "STATUS_" + std::to_string(status);
  }
}

}  // namespace

SolverConfig load_solver_config(const std::filesystem::path& file, const std::string& profile) {
  const std::string src = file.filename().string();
  detail::YamlSection top(detail::parse_yaml(detail::read_text_file(file, src), src), src, "");
  SolverConfig c;
  detail::YamlSection def = top.child("default");
  read_section(def, c, true);
  if (top.has("profiles")) {
    const SolverConfig base = c;
    for (auto& [name, sec] : top.named_sections("profiles")) {
      SolverConfig tmp = base;  // every profile is validated, the requested one is applied
      read_section(sec, tmp, false);
      if (name == profile) c = tmp;
    }
  }
  top.reject_unknown();
  return c;
}

MilpResult solve_day_milp(GRBEnv& env, const DayInstance& day, const MilpOptions& o) {
  MilpModel mm(env, day, o.formulation);
  GRBModel& m = mm.grb();
  MilpResult res;
  res.binaries = mm.binaries();
  res.arcs = mm.prep().arcs_total;
  res.arcs_full = mm.prep().arcs_full;

  // Size statistics and the LP relaxation first, on copies with default (quiet) parameters.
  SolverStats& st = res.stats;
  st.num_vars = m.get(GRB_IntAttr_NumVars);
  st.num_constrs = m.get(GRB_IntAttr_NumConstrs);
  st.num_nz = m.get(GRB_IntAttr_NumNZs);
  {
    GRBModel pre = m.presolve();
    st.presolved_vars = pre.get(GRB_IntAttr_NumVars);
    st.presolved_constrs = pre.get(GRB_IntAttr_NumConstrs);
    st.presolved_nz = pre.get(GRB_IntAttr_NumNZs);
  }

  if (o.compute_lp_bound) {
    GRBModel relaxed = m.relax();
    relaxed.set(GRB_IntParam_OutputFlag, 0);
    relaxed.optimize();
    if (relaxed.get(GRB_IntAttr_Status) == GRB_OPTIMAL) res.lp_bound = relaxed.get(GRB_DoubleAttr_ObjVal);
  }

  // Console output follows OutputFlag of solver.yaml; the log file is written whenever a
  // log directory is given (OutputFlag = 0 would suppress it).
  m.set(GRB_IntParam_OutputFlag, 0);  // quiet while setting parameters (no echo)
  m.set(GRB_IntParam_LogToConsole, o.solver.output_flag);
  m.set(GRB_DoubleParam_TimeLimit, o.solver.time_limit);
  m.set(GRB_DoubleParam_MIPGap, o.solver.mip_gap);
  m.set(GRB_IntParam_Threads, o.solver.threads);
  m.set(GRB_IntParam_Seed, o.solver.seed);
  m.set(GRB_IntParam_MIPFocus, o.solver.mip_focus);
  std::unique_ptr<ConnectivityCuts> cuts;
  if (o.connectivity_cuts) {
    cuts = mm.make_connectivity_cuts(o.per_driver_cuts);
    m.set(GRB_IntParam_PreCrush, 1);  // required for user cuts
    m.setCallback(cuts.get());
  }
  m.set(GRB_IntParam_OutputFlag, (o.solver.output_flag != 0 || !o.log_dir.empty()) ? 1 : 0);
  if (!o.log_dir.empty()) {
    std::filesystem::create_directories(o.log_dir);
    m.set(GRB_StringParam_LogFile, (o.log_dir / (o.tag + ".log")).string());
    if (o.solver.write_lp) m.write((o.log_dir / (o.tag + ".lp")).string());
  }

  if (o.start) res.start_accepted = mm.set_start(*o.start);
  m.optimize();

  const int status = m.get(GRB_IntAttr_Status);
  st.status = status_name(status);
  st.runtime_s = m.get(GRB_DoubleAttr_Runtime);
  st.node_count = m.get(GRB_DoubleAttr_NodeCount);
  if (cuts) res.cuts_added = cuts->cuts_added();
  if (status == GRB_INFEASIBLE || status == GRB_INF_OR_UNBD) {
    if (o.solver.write_iis_on_infeasible && !o.log_dir.empty()) {
      m.computeIIS();
      m.write((o.log_dir / (o.tag + ".ilp")).string());
    }
    throw std::runtime_error("daily MILP infeasible (" + st.status +
                             "): inconsistent input; postponing everything is always feasible");
  }
  if (m.get(GRB_IntAttr_SolCount) > 0) {
    st.objective = m.get(GRB_DoubleAttr_ObjVal);
    st.best_bound = m.get(GRB_DoubleAttr_ObjBound);
    st.gap = m.get(GRB_DoubleAttr_MIPGap);
    res.plan = mm.extract();
    res.plan->solver_stats = st;
  } else {
    st.best_bound = m.get(GRB_DoubleAttr_ObjBound);
  }
  return res;
}

}  // namespace legalvrp::model
