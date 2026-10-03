// T7 (Gurobi): solve wrapper. Acceptance: MILP objective <= baseline objective on every
// `small` day. Plus: solver.yaml profiles, .lp for tiny, IIS on infeasible input, fallback to
// the baseline without an incumbent, and the brief §11 per-day outputs.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "gurobi_c++.h"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/solve_day.hpp"

using namespace legalvrp;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

GRBEnv& env() {
  static GRBEnv e = [] {
    GRBEnv x(true);
    x.set(GRB_IntParam_OutputFlag, 0);
    x.start();
    return x;
  }();
  return e;
}

WeekInstance fixture(const char* name) { return data::read_week(fixtures_dir() / "instances" / name / "1"); }

struct TempDir {
  fs::path path;
  explicit TempDir(const char* tag) : path(fs::temp_directory_path() / (std::string{"legalvrp_t7_"} + tag)) {
    fs::remove_all(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

}  // namespace

TEST_CASE("solver.yaml: default section and profile overrides", "[solve]") {
  const fs::path f = config_dir() / "solver.yaml";
  const auto tiny = model::load_solver_config(f, "tiny");
  CHECK(tiny.time_limit == 60);
  CHECK(tiny.mip_gap == 0.0);
  CHECK(tiny.write_lp);
  const auto small = model::load_solver_config(f, "small");
  CHECK(small.time_limit == 120);
  CHECK(small.mip_gap == 0.01);
  CHECK_FALSE(small.write_lp);
  CHECK(model::load_solver_config(f, "scale").time_limit == 300);
  CHECK(model::load_solver_config(f, "unknown-profile").time_limit == 120);  // default
}

TEST_CASE("solver.yaml: a typo is rejected with its key", "[solve]") {
  TempDir t("yaml");
  fs::create_directories(t.path);
  data::write_text_file(t.path / "solver.yaml",
                        "default: {TimeLimit: 1, MIPGap: 0, Threads: 0, Seed: 0, OutputFlag: 0, MIPFocus: 0,"
                        " write_lp: false, write_iis_on_infeasible: true}\n"
                        "profiles:\n  tiny: {TimeLimt: 5}\n");
  try {
    (void)model::load_solver_config(t.path / "solver.yaml", "tiny");
    FAIL("expected ConfigError");
  } catch (const ConfigError& e) {
    CHECK(e.key() == "profiles.tiny.TimeLimt");
  }
}

TEST_CASE("tiny profile writes the model as .lp and a Gurobi log", "[solve]") {
  TempDir t("lp");
  const DayInstance day = make_day_instance(fixture("tiny"), 0);
  model::MilpOptions o;
  o.solver = model::load_solver_config(config_dir() / "solver.yaml", "tiny");
  o.log_dir = t.path;
  o.tag = "tiny_day0";
  const auto r = model::solve_day_milp(env(), day, o);
  CHECK(r.stats.status == "OPTIMAL");
  CHECK(fs::file_size(t.path / "tiny_day0.lp") > 0);
  CHECK(fs::file_size(t.path / "tiny_day0.log") > 0);
  CHECK(r.stats.num_vars > 0);
  CHECK(r.stats.presolved_vars <= r.stats.num_vars);
}

TEST_CASE("infeasible input: IIS written, then an exception", "[solve]") {
  // A driver already above the weekly service cap: svc <= Hmax - W < 0 with svc >= 0.
  TempDir t("iis");
  DayInstance day = make_day_instance(fixture("tiny"), 0);
  day.states[0].service_minutes_week = 3120 + 100;
  model::MilpOptions o;
  o.log_dir = t.path;
  o.tag = "bad";
  CHECK_THROWS_AS(model::solve_day_milp(env(), day, o), std::runtime_error);
  CHECK(fs::exists(t.path / "bad.ilp"));
}

TEST_CASE("no incumbent at the time limit: the baseline is returned and flagged", "[solve]") {
  const DayInstance day = make_day_instance(fixture("small"), 0);
  model::MilpOptions o;
  o.warm_start = false;
  o.solver.time_limit = 0.0;  // stop before any MILP solution
  const auto r = week::solve_day(env(), day, o);
  CHECK(r.check.ok());
  if (!r.milp.plan) {
    CHECK(r.source == week::PlanSource::baseline_no_incumbent);
    CHECK(r.plan.solver_stats.baseline_fallback);
    CHECK(json(r.plan.routes) == json(r.baseline.routes));
  } else {
    WARN("Gurobi found a solution at time 0; fallback path not exercised");
  }
}

TEST_CASE("per-day outputs: plan, stats, empty violations, Gurobi log", "[solve]") {
  TempDir t("out");
  const DayInstance day = make_day_instance(fixture("tiny"), 0);
  model::MilpOptions o;
  o.solver.mip_gap = 0.0;
  o.log_dir = t.path;
  o.tag = "gurobi";
  const auto r = week::solve_day(env(), day, o);
  week::write_day_outputs(t.path, r);
  for (const char* f : {"plan.json", "stats.json", "violations.json", "gurobi.log"}) {
    INFO(f);
    CHECK(fs::exists(t.path / f));
  }
  const json v = json::parse(data::read_text_file(t.path / "violations.json"));
  CHECK(v.empty());
  const json s = json::parse(data::read_text_file(t.path / "stats.json"));
  CHECK(s.at("source") == "milp");
  CHECK(s.at("status") == "OPTIMAL");
  CHECK(s.at("objective_recomputed").get<double>() <= s.at("baseline_objective").get<double>() + 1e-6);
  const auto plan = json::parse(data::read_text_file(t.path / "plan.json")).get<DayPlan>();
  CHECK(check::check_day(day, plan).ok());
}

TEST_CASE("T7 acceptance: MILP objective <= baseline on every small day", "[solve][acceptance]") {
  const WeekInstance w = fixture("small");
  for (int d = 0; d < w.days; ++d) {
    const DayInstance day = make_day_instance(w, d);
    model::MilpOptions o;
    o.solver.time_limit = 10;
    const auto r = week::solve_day(env(), day, o);
    INFO("day " << d << " source " << week::to_string(r.source));
    CHECK(r.milp.start_accepted);
    CHECK(r.check.ok());
    CHECK(r.plan.objective <= r.baseline_check.objective + 1e-6);
  }
}
