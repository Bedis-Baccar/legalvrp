// Price of myopia — task T11 (PROJECT_BRIEF.md §12): rolling Monday-Friday MILP vs the
// clairvoyant weekly MILP on reduced instances.
//   legalvrp-myopia --config config/instance_myopia.yaml [--seeds 1 2 3] [--day-limit 60]
//                   [--week-limit 600] [--out results/myopia]
// For each seed: certified week; rolling MILP (week::solve_day per day); clairvoyant MILP with
// the rolling plans as MIP start; both weeks checked in week mode; true weekly costs (D-042).
// Price of myopia = rolling - clairvoyant (a lower estimate when the clairvoyant gap > 0; an
// upper estimate is rolling - clairvoyant bound).
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/model/clairvoyant.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/loop.hpp"
#include "legalvrp/week/solve_day.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {
std::string fx(double v, int p = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}
}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Price of myopia: rolling vs clairvoyant weekly MILP"};
  fs::path config, out = results_dir() / "myopia";
  std::vector<std::uint64_t> seeds{1, 2, 3, 4, 5};
  double day_limit = 60, week_limit = 600;
  app.add_option("--config", config, "reduced instance config")->required()->check(CLI::ExistingFile);
  app.add_option("--seeds", seeds, "seeds");
  app.add_option("--day-limit", day_limit, "rolling: TimeLimit per day (s)");
  app.add_option("--week-limit", week_limit, "clairvoyant: TimeLimit (s)");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto ic = data::load_instance_config(config);
    const Config rules = load_config();
    fs::create_directories(out);
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    std::ofstream csv(out / "myopia.csv", std::ios::binary | std::ios::trunc);
    csv << "seed,orders,rolling_cost,clairvoyant_cost,clairvoyant_bound,clairvoyant_gap,clairvoyant_status,"
           "clairvoyant_runtime_s,myopia_eur,myopia_pct,myopia_upper_pct,rolling_postponements,clair_postponements,"
           "rolling_overtime_min,clair_overtime_min,rolling_gini,clair_gini,rolling_km,clair_km,checkers_ok\n";
    std::ostringstream md;
    md << "# Price of myopia (T11)\n\nInstance config `" << config.filename().string() << "`: " << ic.days
       << " days, " << ic.orders_min << "-" << ic.orders_max << " orders/day, " << ic.drivers.size()
       << " drivers. Rolling: strong MILP per day (" << day_limit << " s, MIPGap 0.1 %). Clairvoyant: one "
       << "weekly MILP, rolling plans as start (" << week_limit << " s, MIPGap 0.5 %). Costs are TRUE weekly "
       << "costs (overtime on final weekly hours, D-042), recomputed by the checker. Both weeks pass the "
       << "week-mode checker.\n\n"
       << "| seed | orders | rolling (EUR) | clairvoyant (EUR) | clairvoyant gap | price of myopia | upper estimate "
          "| overtime min R / C | postponements R / C | Gini R / C |\n|---|---|---|---|---|---|---|---|---|---|\n";

    for (const auto seed : seeds) {
      const auto cw = week::generate_certified_week(ic, rules, seed);
      if (!cw) {
        std::cerr << "seed " << seed << ": no certified instance\n";
        continue;
      }
      const WeekInstance& w = cw->generated.week;

      model::MilpOptions day_opt;
      day_opt.solver.time_limit = day_limit;
      day_opt.solver.mip_gap = 0.001;
      const auto rolling = week::run_week(w, [&](const DayInstance& day) {
        return week::solve_day(env, day, day_opt).plan;
      });
      const auto kr = kpi::compute_week_kpis(w, rolling.days, rolling.plans, rolling.week_check);

      model::MilpOptions week_opt;
      week_opt.solver.time_limit = week_limit;
      week_opt.solver.mip_gap = 0.005;
      week_opt.log_dir = out / "logs";
      week_opt.tag = "clairvoyant_s" + std::to_string(seed);
      const auto cl = model::solve_week_clairvoyant(env, w, week_opt, &rolling.plans);
      // Replay the clairvoyant plans through the rolling machinery: same day instances, carry-over
      // and checks as the rolling run, so the two KPI sets are computed identically.
      std::size_t next = 0;
      const auto crun = week::run_week(w, [&](const DayInstance&) { return cl.plans[next++]; });
      const auto kc = kpi::compute_week_kpis(w, crun.days, crun.plans, crun.week_check);

      const double myopia = kr.cost_total - kc.cost_total;
      const double upper = kr.cost_total - std::min(cl.stats.best_bound, kc.cost_total);
      const bool ok = rolling.week_check.ok() && crun.week_check.ok();
      csv << seed << ',' << w.orders.size() << ',' << kr.cost_total << ',' << kc.cost_total << ','
          << cl.stats.best_bound << ',' << cl.stats.gap << ',' << cl.stats.status << ',' << cl.stats.runtime_s << ','
          << myopia << ',' << myopia / kr.cost_total << ',' << upper / kr.cost_total << ','
          << kr.postponement_decisions << ',' << kc.postponement_decisions << ',' << kr.extra_minutes_total << ','
          << kc.extra_minutes_total << ',' << kr.hours_gini << ',' << kc.hours_gini << ',' << kr.km << ',' << kc.km
          << ',' << (ok ? 1 : 0) << '\n';
      csv.flush();
      md << "| " << seed << " | " << w.orders.size() << " | " << fx(kr.cost_total, 2) << " | "
         << fx(kc.cost_total, 2) << " | " << fx(100 * cl.stats.gap) << "% | " << fx(myopia, 2) << " ("
         << fx(100 * myopia / kr.cost_total) << "%) | " << fx(100 * upper / kr.cost_total) << "% | "
         << kr.extra_minutes_total << " / " << kc.extra_minutes_total << " | " << kr.postponement_decisions
         << " / " << kc.postponement_decisions << " | " << fx(kr.hours_gini, 3) << " / " << fx(kc.hours_gini, 3)
         << " |" << (ok ? "" : " CHECKER VIOLATIONS") << "\n";
      std::cout << "seed " << seed << ": rolling " << fx(kr.cost_total, 2) << ", clairvoyant " << fx(kc.cost_total, 2)
                << " (" << cl.stats.status << ", gap " << fx(100 * cl.stats.gap) << "%, " << fx(cl.stats.runtime_s)
                << " s), myopia " << fx(100 * myopia / kr.cost_total) << "%, checkers " << (ok ? "OK" : "VIOLATIONS")
                << std::endl;
    }
    data::write_text_file(out / "myopia.md", md.str());
    std::cout << md.str() << "written: " << out.string() << "\n";
    return 0;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-myopia: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-myopia: " << e.what() << "\n";
    return 2;
  }
}
