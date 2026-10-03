// Week policies — V1-T5.
//   legalvrp-policies lookahead --config config/instance_myopia.yaml [--seeds 1 2 3 4 5]
//                     [--day-limit 60] [--window-limit 120] [--week-limit 600] [--out results/policies]
//     For each seed: certified week; myopic rolling MILP (V0); look-ahead policies (h = 2, 3;
//     scarce-skill reserve with h = 1, 2); clairvoyant weekly MILP (bound). True weekly costs
//     (D-042), every week checked in week mode. Recovery = (myopic - policy) / (myopic - clairvoyant).
//   legalvrp-policies fairness --config config/instance_small.yaml [--seeds ...] [--weights ...]
//                     [--day-limit 10] [--out results/policies]
//     Rolling ALNS week for each fairness weight (EUR per minute of max - min projected weekly
//     hours of the full-time drivers): cost vs Gini curve.
#include <CLI/CLI.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/kpi/kpis.hpp"
#include "legalvrp/model/clairvoyant.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/lookahead.hpp"
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

struct Policy {
  std::string name;
  int horizon;
  double reserve_cost;
};

struct Outcome {
  kpi::WeekKpis k;
  bool ok = false;
  int fallbacks = 0;
  double runtime_s = 0.0;
};

int run_lookahead(GRBEnv& env, const fs::path& config, const std::vector<std::uint64_t>& seeds, double day_limit,
                  double window_limit, double week_limit, double reserve_cost, const fs::path& out) {
  const auto ic = data::load_instance_config(config);
  const Config rules = load_config();
  fs::create_directories(out);
  const std::vector<Policy> policies{{"LA2", 2, 0.0}, {"LA3", 3, 0.0}, {"R1", 1, reserve_cost}, {"LA2+R", 2, reserve_cost}};

  std::ofstream csv(out / "lookahead.csv", std::ios::binary | std::ios::trunc);
  csv << "seed,policy,cost,clairvoyant_cost,myopic_cost,recovery,overtime_min,postponements,unserved_end,gini,km,"
         "fallback_days,runtime_s,checker_ok\n";
  std::map<std::string, std::vector<double>> cost;  // policy -> per seed
  std::vector<double> my, cl;
  std::ostringstream rows;

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

    auto record = [&](const week::WeekRun& run, int fallbacks, double rt) {
      Outcome o;
      o.k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
      o.ok = run.week_check.ok();
      o.fallbacks = fallbacks;
      o.runtime_s = rt;
      return o;
    };
    // Myopic rolling (V0) and the clairvoyant bound.
    const auto myopic = week::run_week(w, [&](const DayInstance& day) { return week::solve_day(env, day, day_opt).plan; });
    const Outcome om = record(myopic, 0, 0.0);
    model::MilpOptions week_opt;
    week_opt.solver.time_limit = week_limit;
    week_opt.solver.mip_gap = 0.005;
    const auto clr = model::solve_week_clairvoyant(env, w, week_opt, &myopic.plans);
    std::size_t next = 0;
    const auto crun = week::run_week(w, [&](const DayInstance&) { return clr.plans[next++]; });
    const Outcome oc = record(crun, 0, clr.stats.runtime_s);
    const double gap = om.k.cost_total - oc.k.cost_total;
    my.push_back(om.k.cost_total);
    cl.push_back(oc.k.cost_total);

    std::vector<std::pair<std::string, Outcome>> all{{"myopic", om}, {"clairvoyant", oc}};
    for (const auto& p : policies) {
      week::LookaheadOptions lo;
      lo.horizon = p.horizon;
      lo.day = day_opt;
      lo.window.solver.time_limit = window_limit;
      lo.window.solver.mip_gap = 0.001;
      lo.reserve_cost = p.reserve_cost;
      int fallbacks = 0;
      double rt = 0.0;
      const auto run = week::run_week_with_context(w, [&](const DayInstance& day, const week::DayContext& ctx) {
        auto r = week::solve_day_lookahead(env, w, day, ctx, lo);
        if (r.source == week::LookaheadSource::fallback) ++fallbacks;
        rt += r.window_stats.runtime_s;
        return r.plan;
      });
      all.emplace_back(p.name, record(run, fallbacks, rt));
      cost[p.name].push_back(all.back().second.k.cost_total);
    }
    for (const auto& [name, o] : all) {
      const double rec = gap > 1e-6 ? (om.k.cost_total - o.k.cost_total) / gap : 0.0;
      csv << seed << ',' << name << ',' << o.k.cost_total << ',' << oc.k.cost_total << ',' << om.k.cost_total << ','
          << rec << ',' << o.k.extra_minutes_total << ',' << o.k.postponement_decisions << ',' << o.k.unserved_end
          << ',' << o.k.hours_gini << ',' << o.k.km << ',' << o.fallbacks << ',' << o.runtime_s << ','
          << (o.ok ? 1 : 0) << '\n';
      std::cout << "seed " << seed << " " << name << ": " << fx(o.k.cost_total, 2) << " EUR"
                << (gap > 1e-6 && name != "myopic" && name != "clairvoyant" ? ", recovery " + fx(100 * rec) + "%" : "")
                << ", overtime " << o.k.extra_minutes_total << " min, unserved " << o.k.unserved_end
                << (o.ok ? "" : ", CHECKER VIOLATIONS") << std::endl;
    }
    csv.flush();
    rows << "| " << seed << " | " << fx(om.k.cost_total, 2) << " | " << fx(oc.k.cost_total, 2);
    for (std::size_t i = 2; i < all.size(); ++i) {
      const double c = all[i].second.k.cost_total;
      rows << " | " << fx(c, 2);
      if (gap > 1e-6) rows << " (" << fx(100 * (om.k.cost_total - c) / gap, 0) << " %)";
      if (!all[i].second.ok) rows << " VIOLATIONS";
    }
    rows << " |\n";
  }

  std::ostringstream md;
  md << "# Week policies: look-ahead and scarce-skill reserve (V1-T5)\n\nConfig `" << config.filename().string()
     << "`. Myopic = rolling MILP per day (" << day_limit << " s, gap 0.1 %). Window MILP " << window_limit
     << " s, gap 0.1 %. Clairvoyant " << week_limit << " s, gap 0.5 %. Reserve cost " << reserve_cost
     << " EUR/min. True weekly costs (EUR), recovery of the myopic - clairvoyant gap in brackets.\n\n"
     << "| seed | myopic | clairvoyant";
  for (const auto& p : policies) md << " | " << p.name;
  md << " |\n|---|---|---";
  for (std::size_t i = 0; i < policies.size(); ++i) md << "|---";
  md << "|\n" << rows.str() << "\n";
  double gap_sum = 0.0;
  for (std::size_t s = 0; s < my.size(); ++s) gap_sum += my[s] - cl[s];
  md << "Aggregate recovery (sum over seeds):";
  for (const auto& p : policies) {
    double rec = 0.0, worst = 0.0;
    for (std::size_t s = 0; s < my.size(); ++s) {
      rec += my[s] - cost[p.name][s];
      worst = std::max(worst, (cost[p.name][s] - my[s]) / my[s]);
    }
    md << " " << p.name << " " << fx(gap_sum > 0 ? 100 * rec / gap_sum : 0.0) << " % (worst vs myopic +"
       << fx(100 * worst, 2) << " %);";
  }
  md << "\n";
  data::write_text_file(out / "lookahead.md", md.str());
  std::cout << md.str();
  return 0;
}

int run_fairness(const fs::path& config, const std::vector<std::uint64_t>& seeds, const std::vector<double>& weights,
                 long long iterations, const fs::path& out) {
  const auto ic = data::load_instance_config(config);
  const Config rules = load_config();
  fs::create_directories(out);
  const std::string tag = config.stem().string();
  std::ofstream csv(out / ("fairness_" + tag + ".csv"), std::ios::binary | std::ios::trunc);
  csv << "seed,weight,cost,gini,spread_h,overtime_min,postponements,unserved_end,km,checker_ok\n";
  struct Acc {
    double cost = 0, gini = 0, spread = 0, overtime = 0;
    int n = 0;
    bool ok = true;
  };
  std::vector<Acc> acc(weights.size());
  for (const auto seed : seeds) {
    const auto cw = week::generate_certified_week(ic, rules, seed);
    if (!cw) {
      std::cerr << "seed " << seed << ": no certified instance\n";
      continue;
    }
    const WeekInstance& w = cw->generated.week;
    for (std::size_t wi = 0; wi < weights.size(); ++wi) {
      alns::Options ao;
      ao.max_iterations = iterations;  // deterministic
      ao.time_limit_s = 1e9;
      ao.fairness_weight = weights[wi];
      const auto run = week::run_week(w, [&](const DayInstance& day) { return alns::solve(day, ao).plan; });
      const auto k = kpi::compute_week_kpis(w, run.days, run.plans, run.week_check);
      double hi = 0, lo = 1e18;
      for (const auto& d : k.drivers) {
        if (d.contract != "full_time") continue;
        hi = std::max(hi, static_cast<double>(d.service_minutes));
        lo = std::min(lo, static_cast<double>(d.service_minutes));
      }
      const double spread_h = (hi - lo) / 60.0;
      csv << seed << ',' << weights[wi] << ',' << k.cost_total << ',' << k.hours_gini << ',' << spread_h << ','
          << k.extra_minutes_total << ',' << k.postponement_decisions << ',' << k.unserved_end << ',' << k.km << ','
          << (run.week_check.ok() ? 1 : 0) << '\n';
      csv.flush();
      auto& a = acc[wi];
      a.cost += k.cost_total;
      a.gini += k.hours_gini;
      a.spread += spread_h;
      a.overtime += static_cast<double>(k.extra_minutes_total);
      ++a.n;
      a.ok = a.ok && run.week_check.ok();
      std::cout << tag << " seed " << seed << " weight " << weights[wi] << ": " << fx(k.cost_total, 2) << " EUR, Gini "
                << fx(k.hours_gini, 3) << ", spread " << fx(spread_h) << " h" << (run.week_check.ok() ? "" : ", VIOLATIONS")
                << std::endl;
    }
  }
  std::ostringstream md;
  md << "# Fairness trade-off (V1-T5), `" << config.filename().string() << "`\n\nRolling ALNS week (" << iterations
     << " iterations per day, deterministic), fairness weight in EUR per minute of (max - min) projected weekly "
        "service of the full-time drivers. Means over seeds; cost = true weekly cost; Gini and spread of the "
        "full-time drivers' weekly hours.\n\n| weight (EUR/min) | cost (EUR) | vs weight 0 | Gini | spread (h) | "
        "overtime (min) | checker |\n|---|---|---|---|---|---|---|\n";
  const double base = acc.empty() || acc[0].n == 0 ? 0.0 : acc[0].cost / acc[0].n;
  for (std::size_t wi = 0; wi < weights.size(); ++wi) {
    const auto& a = acc[wi];
    if (a.n == 0) continue;
    const double c = a.cost / a.n;
    md << "| " << weights[wi] << " | " << fx(c, 2) << " | " << (base > 0 ? "+" + fx(100 * (c - base) / base, 2) + " %" : "")
       << " | " << fx(a.gini / a.n, 3) << " | " << fx(a.spread / a.n) << " | " << fx(a.overtime / a.n, 0) << " | "
       << (a.ok ? "OK" : "VIOLATIONS") << " |\n";
  }
  data::write_text_file(out / ("fairness_" + tag + ".md"), md.str());
  std::cout << md.str();
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Week policies (V1-T5): look-ahead, scarce-skill reserve, fairness"};
  app.require_subcommand(1);
  fs::path config, out = results_dir() / "policies";
  std::vector<std::uint64_t> seeds{1, 2, 3, 4, 5};
  double day_limit = 60, window_limit = 120, week_limit = 600, reserve_cost = 5.0;
  auto* la = app.add_subcommand("lookahead", "look-ahead and reserve vs myopic and clairvoyant");
  la->add_option("--config", config, "instance config")->required()->check(CLI::ExistingFile);
  la->add_option("--seeds", seeds, "seeds");
  la->add_option("--day-limit", day_limit, "myopic MILP TimeLimit per day (s)");
  la->add_option("--window-limit", window_limit, "window MILP TimeLimit (s)");
  la->add_option("--week-limit", week_limit, "clairvoyant TimeLimit (s)");
  la->add_option("--reserve-cost", reserve_cost, "EUR per reserved minute used");
  la->add_option("--out", out, "output directory");
  std::vector<double> weights{0.0, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0};
  long long iterations = 5000;
  auto* fa = app.add_subcommand("fairness", "cost vs Gini curve of the ALNS fairness weight");
  fa->add_option("--config", config, "instance config")->required()->check(CLI::ExistingFile);
  fa->add_option("--seeds", seeds, "seeds");
  fa->add_option("--weights", weights, "fairness weights (EUR per minute of spread)");
  fa->add_option("--iterations", iterations, "ALNS iterations per day");
  fa->add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();
    if (la->parsed()) return run_lookahead(env, config, seeds, day_limit, window_limit, week_limit, reserve_cost, out);
    if (fa->parsed()) return run_fairness(config, seeds, weights, iterations, out);
    return 1;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-policies: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-policies: " << e.what() << "\n";
    return 2;
  }
}
