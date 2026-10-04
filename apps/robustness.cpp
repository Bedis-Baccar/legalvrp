// Robustness of plans to uncertain service times — V1-T6 (evaluation), V1-T7 (robust planning).
//   legalvrp-robustness --config config/instance_small.yaml [--seeds 1 2 3 4 5]
//                       [--solvers baseline alns milp] [--time-limit 30] [--iterations 5000]
//                       [--samples 200] [--noise-only]
//                       [--estimator results/estimators/instance_small.json] [--z 0 0.5 1 1.5 2]
//                       [--reserve 15 30 45 60]
//                       [--out results/robustness]
// For each seed: certified week and its truth (generator side, truth.json). Each solver plans a
// rolling week under each planning policy:
//   v0           the planning rule (service_mu = 10 + 6 x pallets)
//   learned z=Z  the learned estimator (legalvrp-learn), service minutes mu + Z sigma per stop
//   ... +RN      also a pooled time reserve of N minutes before every limit and window end
// Every day's plan is then executed (sim::realise_day) with (a) the week's realised truth and
// (b) `samples` Monte Carlo scenarios (the same for every solver and policy), and judged by the
// checker. Reported: planned cost, realised weekly cost (truth.json, checker in week mode), and
// the share of duties that become late (a service after the window end) or illegal.
#include <CLI/CLI.hpp>

#include <algorithm>
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
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/data/orders.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/estimate/learned.hpp"
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

std::string fx(double v, int p) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
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

struct Policy {
  std::string name;
  bool learned = false;
  double z = 0.0;
  Minutes reserve = 0;
};

// The planned day with the REAL limits (rules, windows, shifts, contracts) of the original week:
// a plan made on a tightened instance (time reserve) is judged against reality.
DayInstance judged(const DayInstance& planned, const WeekInstance& original) {
  DayInstance d = planned;
  d.rules = original.rules;
  d.customers = original.customers;
  d.contracts = original.contracts;
  for (auto& drv : d.drivers) drv = *std::ranges::find(original.drivers, drv.id, &Driver::id);
  return d;
}

struct Sum {
  Tally truth, mc;
  double planned = 0.0, realised = 0.0;
  int postponements = 0, unserved = 0, weeks = 0;
};

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Robustness of plans to uncertain service times (V1-T6, V1-T7)"};
  fs::path config, out = results_dir() / "robustness", estimator_file;
  std::vector<std::uint64_t> seeds{1, 2, 3, 4, 5};
  std::vector<std::string> solvers{"baseline", "alns"};
#ifdef LEGALVRP_HAS_GUROBI
  solvers.push_back("milp");
#endif
  std::vector<double> zs{0.0, 0.5, 1.0, 1.5, 2.0};
  std::vector<Minutes> reserves;
  double time_limit = 30;
  long long iterations = 5000;
  int samples = 200;
  bool noise_only = false, no_v0 = false;
  app.add_option("--config", config, "instance config")->required()->check(CLI::ExistingFile);
  app.add_option("--seeds", seeds, "seeds");
  app.add_option("--solvers", solvers, "baseline, alns, milp");
  app.add_option("--time-limit", time_limit, "MILP TimeLimit per day (s)");
  app.add_option("--iterations", iterations, "ALNS iterations per day (deterministic)");
  app.add_option("--samples", samples, "Monte Carlo scenarios per week");
  app.add_flag("--noise-only", noise_only,
               "true mean = the planning estimate (same cv): separates variance from estimation bias");
  app.add_option("--estimator", estimator_file, "learned estimator (legalvrp-learn): adds the learned policies")
      ->check(CLI::ExistingFile);
  app.add_option("--z", zs, "buffers of the learned policies: mu + z sigma per stop");
  app.add_option("--reserve", reserves, "learned policies also with a pooled time reserve of R minutes (each z x R)");
  app.add_flag("--no-v0", no_v0, "skip the planning-rule policy");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto ic = data::load_instance_config(config);
    const Config rules = load_config();
    fs::create_directories(out);
    std::optional<estimate::LearnedEstimator> est;
    if (!estimator_file.empty()) {
      est.emplace(estimate::model_from_json(nlohmann::json::parse(data::read_text_file(estimator_file))));
    }
    std::vector<Policy> policies;
    if (!no_v0) policies.push_back({"v0", false, 0.0, 0});
    if (est) {
      for (const double z : zs) {
        policies.push_back({"learned z=" + fx(z, 2), true, z, 0});
        for (const Minutes r : reserves) policies.push_back({"learned z=" + fx(z, 2) + " +R" + std::to_string(r), true, z, r});
      }
    }
    const std::string tag = config.stem().string() + (noise_only ? "_noise_only" : "") + (est ? "_learned" : "") + (reserves.empty() ? "" : "_reserve");
#ifdef LEGALVRP_HAS_GUROBI
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();
#endif
    std::ofstream csv(out / ("robustness_" + tag + ".csv"), std::ios::binary | std::ios::trunc);
    csv << "seed,solver,policy,z,reserve,plan_cost,real_cost,postponements,unserved_end,duties,truth_late,truth_illegal,"
           "mc_duties,mc_late,mc_illegal,mc_either,mc_late_stops,mc_stops,mc_mean_overrun_min\n";
    std::vector<std::string> keys;  // "solver | policy", in run order
    std::map<std::string, Sum> sums;

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
      std::vector<std::map<std::string, Minutes>> scenarios;  // the same for every solver and policy
      for (int s = 0; s < samples; ++s) {
        data::Rng rng = data::Rng(seed).split(1000 + static_cast<std::uint64_t>(s));
        scenarios.push_back(sim::sample_truth(truth, rng));
      }

      for (const auto& name : solvers) {
        for (const auto& pol : policies) {
          WeekInstance plan_week = w;
          if (pol.learned) estimate::apply(plan_week, *est, pol.z);
          estimate::reserve_time(plan_week, pol.reserve);
          week::WeekRun run;
          if (name == "baseline") {
            run = week::run_week(plan_week, heuristics::territory_baseline);
          } else if (name == "alns") {
            alns::Options ao;
            ao.max_iterations = iterations;
            ao.time_limit_s = 1e9;
            run = week::run_week(plan_week, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
          } else if (name == "milp") {
#ifdef LEGALVRP_HAS_GUROBI
            model::MilpOptions mo;
            mo.solver.time_limit = time_limit;
            mo.solver.mip_gap = 0.005;
            run = week::run_week(plan_week, [&](const DayInstance& day) { return week::solve_day(env, day, mo).plan; });
#else
            std::cerr << "built without Gurobi: skipping milp\n";
            continue;
#endif
          } else {
            std::cerr << "unknown solver " << name << "\n";
            return 2;
          }
          if (!run.week_check.ok()) throw std::runtime_error(name + ": planned week fails the checker");
          const auto k = kpi::compute_week_kpis(plan_week, run.days, run.plans, run.week_check);

          // (a) truth.json: per-duty outcomes and the realised weekly cost (checker in week mode).
          Tally t_truth, t_mc;
          WeekInstance real_week = w;  // the real limits
          for (auto& o : real_week.orders) o.service_mu = truth.minutes.at(o.id);
          std::vector<DayInstance> judge;
          for (const auto& d : run.days) judge.push_back(judged(d, w));
          std::vector<DayInstance> real_days;
          std::vector<DayPlan> real_plans;
          for (std::size_t d = 0; d < run.plans.size(); ++d) {
            auto r = sim::realise_day(judge[d], run.plans[d], truth.minutes);
            t_truth.add(r);
            real_days.push_back(std::move(r.day));
            real_plans.push_back(std::move(r.plan));
          }
          const auto real_check = check::check_week(real_week, real_plans);
          const auto rk = kpi::compute_week_kpis(real_week, real_days, real_plans, real_check);
          // (b) Monte Carlo.
          for (const auto& minutes : scenarios) {
            for (std::size_t d = 0; d < run.plans.size(); ++d) {
              t_mc.add(sim::realise_day(judge[d], run.plans[d], minutes));
            }
          }
          csv << seed << ',' << name << ',' << pol.name << ',' << pol.z << ',' << pol.reserve << ',' << k.cost_total << ',' << rk.cost_total
              << ',' << k.postponement_decisions << ',' << k.unserved_end << ',' << t_truth.duties << ','
              << t_truth.late << ',' << t_truth.illegal << ',' << t_mc.duties << ',' << t_mc.late << ','
              << t_mc.illegal << ',' << t_mc.either << ',' << t_mc.late_stops << ',' << t_mc.stops << ','
              << (t_mc.duties > 0 ? t_mc.overrun / static_cast<double>(t_mc.duties) : 0.0) << '\n';
          csv.flush();
          const std::string key = name + " | " + pol.name;
          if (!sums.contains(key)) keys.push_back(key);
          Sum& s = sums[key];
          s.truth.add(t_truth);
          s.mc.add(t_mc);
          s.planned += k.cost_total;
          s.realised += rk.cost_total;
          s.postponements += k.postponement_decisions;
          s.unserved += k.unserved_end;
          ++s.weeks;
          std::cout << tag << " seed " << seed << " " << key << ": planned " << fx(k.cost_total, 2) << ", realised "
                    << fx(rk.cost_total, 2) << "; MC late or illegal "
                    << pct(static_cast<double>(t_mc.either), static_cast<double>(t_mc.duties)) << std::endl;
        }
      }
    }

    std::ostringstream md;
    md << "# Robustness to true service times, `" << config.filename().string() << "`\n\n"
       << "Plans executed with the true durations (lognormal by customer type, `service_true` in the config). "
          "Seeds " << seeds.front() << "-" << seeds.back() << "; Monte Carlo: " << samples
       << " scenarios per week. A duty is *late* if a service starts after the window end, *illegal* if any "
          "other rule breaks in reality. Policies: `v0` = planning rule 10 + 6 x pallets; `learned z=Z` = "
          "learned estimator, mu + Z sigma per stop; `+RN` = also a pooled time reserve of N minutes before every limit and window end (the plan is judged against the real limits). Planned cost = the plan's weekly cost; realised cost = "
          "the same plans executed with `truth.json` (true weekly cost, checker in week mode)."
       << (noise_only ? " **Noise only**: true mean = the planning estimate, same cv (no estimation bias)." : "")
       << "\n\n| solver | policy | planned cost (EUR) | realised cost (EUR) | postponements | unserved | late (MC) | "
          "illegal (MC) | late or illegal (MC) | vs v0 | late stops (MC) | mean overrun (min) |\n"
          "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& key : keys) {
      const Sum& s = sums[key];
      const double n = static_cast<double>(s.weeks);
      const auto bar = key.find(" | ");
      const std::string solver = key.substr(0, bar);
      const auto v0 = sums.find(solver + " | v0");
      std::string vs = "";
      if (v0 != sums.end() && v0->second.mc.either > 0 && key != v0->first) {
        const double a = static_cast<double>(s.mc.either) / static_cast<double>(s.mc.duties);
        const double b = static_cast<double>(v0->second.mc.either) / static_cast<double>(v0->second.mc.duties);
        vs = fx(100.0 * (a - b) / b, 0) + " %";
      }
      md << "| " << solver << " | " << key.substr(bar + 3) << " | " << fx(s.planned / n, 2) << " | "
         << fx(s.realised / n, 2) << " | " << fx(s.postponements / n, 1) << " | " << fx(s.unserved / n, 1) << " | "
         << pct(static_cast<double>(s.mc.late), static_cast<double>(s.mc.duties)) << " | "
         << pct(static_cast<double>(s.mc.illegal), static_cast<double>(s.mc.duties)) << " | "
         << pct(static_cast<double>(s.mc.either), static_cast<double>(s.mc.duties)) << " | " << vs << " | "
         << pct(static_cast<double>(s.mc.late_stops), static_cast<double>(s.mc.stops)) << " | "
         << fx(s.mc.duties > 0 ? s.mc.overrun / static_cast<double>(s.mc.duties) : 0.0, 1) << " |\n";
    }
    md << "\nRules broken in reality (Monte Carlo, duties):\n\n";
    for (const auto& key : keys) {
      md << "- " << key << ":";
      for (const auto& [rule, n] : sums[key].mc.rules) md << " " << rule << " " << n << ";";
      md << "\n";
    }
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
