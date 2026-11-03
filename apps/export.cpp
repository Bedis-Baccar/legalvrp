// Run bundle of the visual application from stored plans (docs/APP_PLAN.md, A1).
//   legalvrp-export --instance data/instances/small/1 --plans results/small_1_alns --out bundle.json
//                   [--run NAME] [--solver LABEL] [--no-truth]
//   legalvrp-export --duties tests/fixtures/duties.json --out gallery.json      (checker's gallery)
// Reads <plans>/day<d>/plan.json for every day of the week, replays the rolling week with these
// plans (carried orders, escalating penalties, weekly state from the checker), and writes the
// bundle: timelines, checker verdicts (violations included: hand-made illegal plans are drawn as
// they would unfold), KPIs, and with truth.json the realised days.
// Plans made on a modified instance (learned durations, time reserve) must be exported by
// legalvrp-run-week --bundle, which knows that instance.
// --duties: the labelled duty corpus as the checker's gallery (viz::duty_gallery), every item with
// its timeline, the checker's violations and whether they match the label.
// Exit code 0 = written and the week passes the checker, 1 = written with violations, 2 = error.
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/viz/bundle.hpp"
#include "legalvrp/viz/gallery.hpp"
#include "legalvrp/week/loop.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

int main(int argc, char** argv) {
  CLI::App app{"Run bundle of the visual application from stored plans"};
  app.set_version_flag("--version", std::string{kVersion});
  fs::path instance, plans_dir, out, duties;
  std::string run, solver = "hand";
  bool no_truth = false;
  auto* inst = app.add_option("--instance", instance, "instance directory (week.json, matrix.json[, truth.json])")
                   ->check(CLI::ExistingDirectory);
  auto* pl = app.add_option("--plans", plans_dir, "directory with day<d>/plan.json")->check(CLI::ExistingDirectory);
  auto* du = app.add_option("--duties", duties, "labelled duty corpus (tests/fixtures/duties.json)")
                 ->check(CLI::ExistingFile);
  inst->needs(pl);
  pl->needs(inst);
  du->excludes(inst);
  du->excludes(pl);
  app.add_option("--out", out, "bundle file")->required();
  app.add_option("--run", run, "run name (default: the plans directory name)");
  app.add_option("--solver", solver, "label shown by the application (default hand)");
  app.add_flag("--no-truth", no_truth, "do not add the realised days even if truth.json exists");
  CLI11_PARSE(app, argc, argv);

  if (duties.empty() && instance.empty()) {
    std::cerr << "legalvrp-export: give --instance and --plans, or --duties\n";
    return 2;
  }

  try {
    if (!duties.empty()) {
      const auto g = viz::duty_gallery(nlohmann::json::parse(data::read_text_file(duties)), load_config());
      int mismatches = 0;
      for (const auto& it : g.at("items")) mismatches += it.at("matches").get<bool>() ? 0 : 1;
      data::write_text_file(out, g.dump());
      std::cout << "gallery: " << out.string() << " (" << g.at("items").size() << " duties, " << mismatches
                << " label mismatch(es))\n";
      return mismatches == 0 ? 0 : 1;
    }
    const WeekInstance week = data::read_week(instance);
    std::vector<DayPlan> stored;
    for (int d = 0; d < week.days; ++d) {
      stored.push_back(nlohmann::json::parse(data::read_text_file(plans_dir / ("day" + std::to_string(d)) / "plan.json"))
                           .get<DayPlan>());
    }
    const auto result = week::run_week(week, [&](const DayInstance& day) { return stored.at(static_cast<std::size_t>(day.day)); });
    const auto k = kpi::compute_week_kpis(week, result.days, result.plans, result.week_check);

    viz::RunData rd;
    rd.run = run.empty() ? plans_dir.filename().string() : run;
    rd.solver = solver;
    rd.week = &week;
    rd.days = &result.days;
    rd.plans = &result.plans;
    rd.day_checks = &result.day_checks;
    rd.week_check = &result.week_check;
    rd.kpis = &k;
    std::optional<data::TrueService> truth;
    if (!no_truth && fs::exists(instance / "truth.json")) {
      truth = data::read_truth(instance);
      rd.truth = &*truth;
    }
    rd.options = {{"solver", solver}, {"source", "stored plans"}};
    data::write_text_file(out, viz::run_bundle(rd).dump());
    std::cout << "bundle: " << out.string() << " (" << result.week_check.violations.size() << " violation(s))\n";
    return result.week_check.ok() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-export: " << e.what() << "\n";
    return 2;
  }
}
