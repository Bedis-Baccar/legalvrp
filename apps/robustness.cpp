// Robustness of deterministic plans to uncertain service times — V1-T6.
//   legalvrp-robustness --config config/instance_small.yaml [--seeds 1 2 3 4 5]
//                       [--solvers baseline alns milp] [--time-limit 30] [--iterations 5000]
//                       [--samples 200] [--noise-only] [--out results/robustness]
// For each seed: certified week and its truth (generator side, truth.json); a rolling week is
// planned with the estimates (service_mu) by each solver; every day's plan is then executed
// (sim::realise_day) with (a) the week's realised truth and (b) `samples` Monte Carlo scenarios
// drawn from the true distributions, and judged by the checker. Reported per solver: share of
// duties that become late (a service after the window end) or illegal (any other rule).
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "legalvrp/alns/alns.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/sim/realise.hpp"
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

std::string pct(double num, double den) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(1) << (den > 0 ? 100.0 * num / den : 0.0) << " %";
  return os.str();
}

struct Tally {
  long long duties = 0, late = 0, illegal = 0, either = 0, stops = 0, late_stops = 0;
  double overrun = 0.0;  // minutes, summed over duties
  std::map<std::string, long long> rules;
  void add(const sim::RealisedDay& r) {
    for (const auto& d : r.duties) {
      ++duties;
      late += d.late ? 1 : 0;
      illegal += d.illegal ? 1 : 0;
      either += d.late || d.illegal ? 1 : 0;
      stops += d.stops;
      late_stops += d.late_stops;
      overrun += static_cast<double>(d.overrun);
      for (const auto& rule : d.rules) ++rules[rule];
    }
  }
  void add(const Tally& t) {
    duties += t.duties;
    late += t.late;
    illegal += t.illegal;
    either += t.either;
    stops += t.stops;
    late_stops += t.late_stops;
    overrun += t.overrun;
    for (const auto& [k, v] : t.rules) rules[k] += v;
  }
};

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Robustness of deterministic plans to uncertain service times (V1-T6)"};
  fs::path config, out = results_dir() / "robustness";
  std::vector<std::uint64_t> seeds{1, 2, 3, 4, 5};
  std::vector<std::string> solvers{"baseline", "alns"};
#ifdef LEGALVRP_HAS_GUROBI
  solvers.push_back("milp");
#endif
  double time_limit = 30;
  long long iterations = 5000;
  int samples = 200;
  bool noise_only = false;
  app.add_option("--config", config, "instance config")->required()->check(CLI::ExistingFile);
  app.add_option("--seeds", seeds, "seeds");
  app.add_option("--solvers", solvers, "baseline, alns, milp");
  app.add_option("--time-limit", time_limit, "MILP TimeLimit per day (s)");
  app.add_option("--iterations", iterations, "ALNS iterations per day (deterministic)");
  app.add_option("--samples", samples, "Monte Carlo scenarios per week");
  app.add_flag("--noise-only", noise_only,
               "true mean = the planning estimate (same cv): separates variance from estimation bias");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto ic = data::load_instance_config(config);
    const Config rules = load_config();
    fs::create_directories(out);
    const std::string tag = config.stem().string() + (noise_only ? "_noise_only" : "");
#ifdef LEGALVRP_HAS_GUROBI
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();
#endif
    std::ofstream csv(out / ("robustness_" + tag + ".csv"), std::ios::binary | std::ios::trunc);
    csv << "seed,solver,plan_cost,duties,truth_late,truth_illegal,mc_duties,mc_late,mc_illegal,mc_either,"
           "mc_late_stops,mc_stops,mc_mean_overrun_min\n";
    std::map<std::string, Tally> truth_all, mc_all;
    std::map<std::string, double> cost_all;

    for (const auto seed : seeds) {
      const auto cw = week::generate_certified_week(ic, rules, seed);
      if (!cw) {
        std::cerr << "seed " << seed << ": no certified instance\n";
        continue;
      }
      const WeekInstance& w = cw->generated.week;
      data::TrueService truth = cw->generated.truth;
      if (noise_only) {  // unbiased truth: mean = service_mu, same coefficient of variation
        data::Rng rng = data::Rng(seed).split(999);
        for (const auto& o : w.orders) {
          const double cv = truth.mean.at(o.id) > 0.0 ? truth.sigma.at(o.id) / truth.mean.at(o.id) : 0.0;
          truth.mean[o.id] = static_cast<double>(o.service_mu);
          truth.sigma[o.id] = cv * static_cast<double>(o.service_mu);
          truth.minutes[o.id] = data::sample_service(truth.mean[o.id], cv, rng);
        }
      }
      for (const auto& name : solvers) {
        week::WeekRun run;
        if (name == "baseline") {
          run = week::run_week(w, heuristics::territory_baseline);
        } else if (name == "alns") {
          alns::Options ao;
          ao.max_iterations = iterations;
          ao.time_limit_s = 1e9;
          run = week::run_week(w, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
        } else if (name == "milp") {
#ifdef LEGALVRP_HAS_GUROBI
          model::MilpOptions mo;
          mo.solver.time_limit = time_limit;
          mo.solver.mip_gap = 0.005;
          run = week::run_week(w, [&](const DayInstance& day) { return week::solve_day(env, day, mo).plan; });
#else
          std::cerr << "built without Gurobi: skipping milp\n";
          continue;
#endif
        } else {
          std::cerr << "unknown solver " << name << "\n";
          return 2;
        }
        if (!run.week_check.ok()) throw std::runtime_error(name + ": planned week fails the checker");
        const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);

        Tally t_truth, t_mc;
        for (std::size_t d = 0; d < run.plans.size(); ++d) {
          t_truth.add(sim::realise_day(run.days[d], run.plans[d], truth.minutes));
        }
        for (int s = 0; s < samples; ++s) {  // same scenarios for every solver
          data::Rng rng = data::Rng(seed).split(1000 + static_cast<std::uint64_t>(s));
          const auto minutes = sim::sample_truth(truth, rng);
          for (std::size_t d = 0; d < run.plans.size(); ++d) {
            t_mc.add(sim::realise_day(run.days[d], run.plans[d], minutes));
          }
        }
        csv << seed << ',' << name << ',' << k.cost_total << ',' << t_truth.duties << ',' << t_truth.late << ','
            << t_truth.illegal << ',' << t_mc.duties << ',' << t_mc.late << ',' << t_mc.illegal << ',' << t_mc.either
            << ',' << t_mc.late_stops << ',' << t_mc.stops << ','
            << (t_mc.duties > 0 ? t_mc.overrun / static_cast<double>(t_mc.duties) : 0.0) << '\n';
        csv.flush();
        truth_all[name].add(t_truth);
        mc_all[name].add(t_mc);
        cost_all[name] += k.cost_total;
        std::cout << tag << " seed " << seed << " " << name << ": cost " << k.cost_total << ", truth: late "
                  << pct(static_cast<double>(t_truth.late), static_cast<double>(t_truth.duties)) << ", illegal "
                  << pct(static_cast<double>(t_truth.illegal), static_cast<double>(t_truth.duties)) << "; MC: late "
                  << pct(static_cast<double>(t_mc.late), static_cast<double>(t_mc.duties)) << ", illegal "
                  << pct(static_cast<double>(t_mc.illegal), static_cast<double>(t_mc.duties)) << std::endl;
      }
    }

    std::ostringstream md;
    md << "# Robustness to true service times (V1-T6), `" << config.filename().string() << "`\n\n"
       << "Plans made with the estimate 10 + 6 x pallets, executed with the true durations (lognormal by "
          "customer type, `service_true` in the config). Seeds " << seeds.front() << "-" << seeds.back()
       << "; Monte Carlo: " << samples << " scenarios per week. A duty is *late* if a service starts after "
          "the window end, *illegal* if any other rule breaks in reality."
       << (noise_only ? " **Noise only**: true mean = the planning estimate, same cv (no estimation bias)." : "")
       << "\n\n"
       << "| solver | planned cost (EUR, mean) | duties | late (truth.json) | illegal (truth.json) | late (MC) | "
          "illegal (MC) | late or illegal (MC) | late stops (MC) | mean overrun (min) |\n"
          "|---|---|---|---|---|---|---|---|---|---|\n";
    const double n_seeds = static_cast<double>(seeds.size());
    for (const auto& name : solvers) {
      if (!mc_all.contains(name)) continue;
      const Tally& a = truth_all[name];
      const Tally& b = mc_all[name];
      md << "| " << name << " | " << std::fixed << std::setprecision(2) << cost_all[name] / n_seeds << " | "
         << a.duties << " | " << pct(static_cast<double>(a.late), static_cast<double>(a.duties)) << " | "
         << pct(static_cast<double>(a.illegal), static_cast<double>(a.duties)) << " | "
         << pct(static_cast<double>(b.late), static_cast<double>(b.duties)) << " | "
         << pct(static_cast<double>(b.illegal), static_cast<double>(b.duties)) << " | "
         << pct(static_cast<double>(b.either), static_cast<double>(b.duties)) << " | "
         << pct(static_cast<double>(b.late_stops), static_cast<double>(b.stops)) << " | " << std::setprecision(1)
         << (b.duties > 0 ? b.overrun / static_cast<double>(b.duties) : 0.0) << " |\n";
    }
    md << "\nRules broken in reality (Monte Carlo, duties):";
    for (const auto& name : solvers) {
      if (!mc_all.contains(name)) continue;
      md << " " << name << " {";
      for (const auto& [rule, n] : mc_all[name].rules) md << " " << rule << ": " << n;
      md << " };";
    }
    md << "\n";
    data::write_text_file(out / ("robustness_" + tag + ".md"), md.str());
    std::cout << md.str();
    return 0;
#ifdef LEGALVRP_HAS_GUROBI
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-robustness: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
#endif
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-robustness: " << e.what() << "\n";
    return 2;
  }
}
