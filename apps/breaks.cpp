// Saving of the V1-T8 break rules over the V0 rules.
//   legalvrp-breaks --config config/instance_small.yaml [--size N] [--seeds 1 2 3 4 5]
//                   [--iterations 5000] [--milp-limit 0] [--out results/breaks]
// For each seed: one certified week, planned twice by the rolling loop, under the V0 break rules
// (one 45-min break) and under the V1 rules (+ a lone 30-min break, + the 15 + 30 split):
// ALNS (deterministic, `iterations` per day) and, if --milp-limit > 0, the MILP (s per day).
// Reported: true weekly cost, postponements, unserved, paid minutes (temps de service),
// duties per break pattern. `--size N` reads `config` as a size family (instance_large.yaml).
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/loop.hpp"
#ifdef LEGALVRP_HAS_GUROBI
#include "gurobi_c++.h"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/solve_day.hpp"
#endif

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {

std::string fx(double v, int p = 2) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}

struct Outcome {
  double cost = 0.0;
  int postponements = 0, unserved = 0, duties = 0, full = 0, short_break = 0, split = 0;
  long long paid = 0;
};

Outcome measure(const WeekInstance& w, const week::WeekRun& run) {
  const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
  Outcome o;
  o.cost = k.cost_total;
  o.postponements = k.postponement_decisions;
  o.unserved = k.unserved_end;
  for (const auto& d : k.drivers) o.paid += d.service_minutes;
  for (const auto& p : run.plans) {
    for (const auto& r : p.routes) {
      if (r.order_ids.empty()) continue;
      ++o.duties;
      if (r.breaks.size() == 2) {
        ++o.split;
      } else if (r.breaks.size() == 1) {
        (r.breaks[0].minutes == w.rules.break_length ? o.full : o.short_break) += 1;
      }
    }
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Saving of the V1-T8 break rules over V0"};
  fs::path config, out = results_dir() / "breaks";
  std::vector<std::uint64_t> seeds{1, 2, 3, 4, 5};
  long long iterations = 5000;
  double milp_limit = 0;
  int size = 0;
  app.add_option("--config", config, "instance config (or size family with --size)")->required()->check(CLI::ExistingFile);
  app.add_option("--size", size, "orders per day of a size family");
  app.add_option("--seeds", seeds, "seeds");
  app.add_option("--iterations", iterations, "ALNS iterations per day");
  app.add_option("--milp-limit", milp_limit, "MILP TimeLimit per day (s); 0 = no MILP");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto ic = size > 0 ? data::scale_instance(data::load_scale_config(config), size)
                             : data::load_instance_config(config);
    const Config rules = load_config();
    fs::create_directories(out);
    const std::string tag = size > 0 ? ic.name : config.stem().string();
#ifdef LEGALVRP_HAS_GUROBI
    std::optional<GRBEnv> env;
    if (milp_limit > 0) {
      env.emplace(true);
      env->set(GRB_IntParam_OutputFlag, 0);
      env->start();
    }
#endif
    std::vector<std::string> solvers{"alns"};
    if (milp_limit > 0) solvers.push_back("milp");
    std::ofstream csv(out / ("breaks_" + tag + ".csv"), std::ios::binary | std::ios::trunc);
    csv << "seed,solver,rules,cost,postponements,unserved_end,paid_min,duties,full,short,split\n";
    std::map<std::string, Outcome> sum;  // "solver|rules"
    int weeks = 0;
    for (const auto seed : seeds) {
      const auto cw = week::generate_certified_week(ic, rules, seed);
      if (!cw) {
        std::cerr << "seed " << seed << ": no certified instance\n";
        continue;
      }
      ++weeks;
      for (const auto& solver : solvers) {
        for (const bool v1 : {false, true}) {
          WeekInstance w = cw->generated.week;
          w.rules.allow_short_break = v1;
          w.rules.allow_split_break = v1;
          week::WeekRun run;
          if (solver == "alns") {
            alns::Options ao;
            ao.max_iterations = iterations;
            ao.time_limit_s = 1e9;
            run = week::run_week(w, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
          } else {
#ifdef LEGALVRP_HAS_GUROBI
            model::MilpOptions mo;
            mo.solver.time_limit = milp_limit;
            mo.solver.mip_gap = 0.001;
            run = week::run_week(w, [&](const DayInstance& day) { return week::solve_day(*env, day, mo).plan; });
#else
            throw std::runtime_error("built without Gurobi: no --milp-limit");
#endif
          }
          if (!run.week_check.ok()) throw std::runtime_error(solver + ": week fails the checker");
          const Outcome o = measure(w, run);
          const std::string name = v1 ? "V1" : "V0";
          csv << seed << ',' << solver << ',' << name << ',' << o.cost << ',' << o.postponements << ',' << o.unserved
              << ',' << o.paid << ',' << o.duties << ',' << o.full << ',' << o.short_break << ',' << o.split << '\n';
          csv.flush();
          Outcome& s = sum[solver + "|" + name];
          s.cost += o.cost;
          s.postponements += o.postponements;
          s.unserved += o.unserved;
          s.paid += o.paid;
          s.duties += o.duties;
          s.full += o.full;
          s.short_break += o.short_break;
          s.split += o.split;
          std::cout << tag << " seed " << seed << " " << solver << " " << name << ": " << fx(o.cost) << " EUR, "
                    << o.postponements << " postponements, " << o.unserved << " unserved, duties " << o.duties
                    << " (45: " << o.full << ", 30: " << o.short_break << ", 15+30: " << o.split << ")" << std::endl;
        }
      }
    }
    std::ostringstream md;
    md << "# Break rules V1-T8 vs V0, `" << tag << "`\n\nSeeds " << seeds.front() << "-" << seeds.back() << " (" << weeks
       << " certified weeks). V0 = one 45-min break; V1 = + a lone 30-min break (work <= 9 h, driving <= 4 h 30) "
          "and the 15 + 30 split. ALNS " << iterations << " iterations/day"
       << (milp_limit > 0 ? ", MILP " + fx(milp_limit, 0) + " s/day" : std::string{}) << ". Means per week.\n\n"
       << "| solver | rules | cost (EUR) | vs V0 | postponements | unserved | paid hours | duties | with 45 | with 30 | "
          "with 15+30 |\n|---|---|---|---|---|---|---|---|---|---|---|\n";
    const double n = weeks > 0 ? weeks : 1;
    for (const auto& solver : solvers) {
      const Outcome& a = sum[solver + "|V0"];
      for (const std::string name : {"V0", "V1"}) {
        const Outcome& s = sum[solver + "|" + name];
        md << "| " << solver << " | " << name << " | " << fx(s.cost / n) << " | "
           << (name == "V1" && a.cost > 0 ? fx(100.0 * (s.cost - a.cost) / a.cost, 2) + " %" : std::string{}) << " | "
           << fx(s.postponements / n, 1) << " | " << fx(s.unserved / n, 1) << " | " << fx(static_cast<double>(s.paid) / n / 60.0, 1) << " | "
           << fx(s.duties / n, 1) << " | " << fx(s.full / n, 1) << " | " << fx(s.short_break / n, 1) << " | "
           << fx(s.split / n, 1) << " |\n";
      }
    }
    data::write_text_file(out / ("breaks_" + tag + ".md"), md.str());
    std::cout << md.str();
    return 0;
#ifdef LEGALVRP_HAS_GUROBI
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-breaks: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
#endif
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-breaks: " << e.what() << "\n";
    return 2;
  }
}
