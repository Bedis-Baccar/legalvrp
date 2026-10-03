// Rolling Monday-Friday week — task T8 (PROJECT_BRIEF.md §8 algorithm, §11 outputs).
//   legalvrp-run-week --instance data/instances/small/1 [--solver milp|baseline]
//                     [--profile small] [--time-limit S] [--fairness W] [--run NAME]
// Writes results/<run>/day<d>/{plan.json, stats.json, violations.json, gurobi.log} and
// results/<run>/{week_kpis.json, week_report.md}. Exit code 0 only if the week-mode checker
// reports zero violations.
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/kpi/report.hpp"
#include "legalvrp/week/loop.hpp"
#ifdef LEGALVRP_HAS_GUROBI
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/solve_day.hpp"
#endif

namespace fs = std::filesystem;
using namespace legalvrp;

int main(int argc, char** argv) {
  CLI::App app{"Run the rolling Monday-Friday week"};
  app.set_version_flag("--version", std::string{kVersion});
  fs::path instance;
  std::string solver_name = "milp", profile, run;
  double time_limit = -1;
  double fairness = 0.2;
  app.add_option("--instance", instance, "data/instances/<name>/<seed>")->required()->check(CLI::ExistingDirectory);
  app.add_option("--solver", solver_name, "milp (default), alns or baseline")
      ->check(CLI::IsMember({"milp", "alns", "baseline"}));
  app.add_option("--profile", profile, "solver.yaml profile (default: the instance name)");
  app.add_option("--time-limit", time_limit, "override TimeLimit per day (s)");
  app.add_option("--fairness", fairness, "ALNS: EUR per minute of spread of full-time weekly hours (D-108)");
  app.add_option("--run", run, "run name (default <name>_<seed>_<solver>)");
  CLI11_PARSE(app, argc, argv);

  try {
    const WeekInstance week = data::read_week(instance);
    if (run.empty()) run = week.name + "_" + std::to_string(week.seed) + "_" + solver_name;
    const fs::path out = results_dir() / run;
    auto day_dir = [&](int d) { return out / ("day" + std::to_string(d)); };

    week::WeekRun result;
    if (solver_name == "alns") {  // V1-T2: time limit per day (default 30 s)
      alns::Options ao;
      ao.time_limit_s = time_limit > 0 ? time_limit : 30.0;
      ao.fairness_weight = fairness;
      result = week::run_week(week, [&](const DayInstance& day) {
        const auto r = alns::solve(day, ao);
        std::cout << "day " << day.day << ": " << day.orders.size() << " orders, ALNS " << r.iterations
                  << " iterations in " << r.runtime_s << " s, cost " << r.cost << "\n";
        return r.plan;
      });
      for (std::size_t d = 0; d < result.plans.size(); ++d) {
        const fs::path dir = day_dir(static_cast<int>(d));
        data::write_text_file(dir / "plan.json", to_canonical_text(nlohmann::json(result.plans[d])));
        data::write_text_file(dir / "violations.json",
                              to_canonical_text(nlohmann::json(result.day_checks[d].violations)));
      }
    } else if (solver_name == "baseline") {
      result = week::run_week(week, heuristics::territory_baseline);
      for (std::size_t d = 0; d < result.plans.size(); ++d) {
        const fs::path dir = day_dir(static_cast<int>(d));
        data::write_text_file(dir / "plan.json", to_canonical_text(nlohmann::json(result.plans[d])));
        data::write_text_file(dir / "violations.json",
                              to_canonical_text(nlohmann::json(result.day_checks[d].violations)));
      }
    } else {
#ifdef LEGALVRP_HAS_GUROBI
      model::MilpOptions opt;
      opt.solver = model::load_solver_config(config_dir() / "solver.yaml", profile.empty() ? week.name : profile);
      if (time_limit > 0) opt.solver.time_limit = time_limit;
      GRBEnv env(true);
      env.set(GRB_IntParam_OutputFlag, 0);
      env.start();
      result = week::run_week(week, [&](const DayInstance& day) {
        model::MilpOptions o = opt;
        o.log_dir = day_dir(day.day);
        o.tag = "gurobi";
        const auto s = week::solve_day(env, day, o);
        week::write_day_outputs(day_dir(day.day), s);
        std::cout << "day " << day.day << ": " << day.orders.size() << " orders, " << s.milp.stats.status
                  << " " << s.milp.stats.runtime_s << " s, gap " << 100.0 * s.milp.stats.gap << "%, plan from "
                  << week::to_string(s.source) << ", cost " << s.check.objective << " (baseline "
                  << s.baseline_check.objective << ")\n";
        return s.plan;
      });
#else
      std::cerr << "legalvrp-run-week: built without Gurobi; use --solver baseline\n";
      return 2;
#endif
    }

    const kpi::WeekKpis k = kpi::compute_week_kpis(week, result.days, result.plans, result.week_check);
    kpi::write_week_report(out, k, "Week report — " + run);
    std::cout << "week: cost " << k.cost_total << " EUR, " << k.km << " km, " << k.served << " served, "
              << k.unserved_end << " unserved at the end, gini " << k.hours_gini << ", checker "
              << (k.week_checker_ok ? "OK" : "VIOLATIONS") << "\nwritten: " << (out / "week_report.md").string()
              << "\n";
    return k.week_checker_ok ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-run-week: " << e.what() << "\n";
    return 2;
  }
}
