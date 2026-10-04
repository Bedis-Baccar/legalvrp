// Column generation lower bounds on the V1-T4 instances — V1-T9.
//   legalvrp-cg --config config/instance_scale.yaml [--orders 8 10 ... 100] [--seeds 1 2 3 4 5]
//               [--cg-time 120] [--mip-time 30] [--v0-rules] [--compare-csv docs/compare/compare.csv]
//               [--out results/cg]
// For each (n, seed): the certified one-day instance of V1-T4 (same generator, sizes and seeds);
// ALNS as in V1-T4 (same time T and seed, route pool collected); column generation (cg::solve)
// started from the ALNS pool and routes: a valid lower bound and an upper bound. Every plan is
// validated by the checker. Certified gap of a solution = (cost - best lower bound) / cost.
// --v0-rules plans with the V0 break rules, as in V1-T4, so that the MILP bounds of
// compare.csv apply to the same problem.
#include <CLI/CLI.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/cg/colgen.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/week/certify.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {

std::string fx(double v, int p = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}
double median(std::vector<double> v) {
  if (v.empty()) return NAN;
  std::ranges::sort(v);
  const std::size_t m = v.size() / 2;
  return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

struct MilpRef {
  double objective = NAN, bound = NAN;
};

// compare.csv of V1-T4: n,K,seed,T,baseline,alns,hybrid,milp,milp_bound,...
std::map<std::pair<int, std::uint64_t>, MilpRef> read_compare(const fs::path& file) {
  std::map<std::pair<int, std::uint64_t>, MilpRef> out;
  std::ifstream in(file);
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) f.push_back(cell);
    if (f.size() < 9 || f[7].empty() || f[8].empty()) continue;
    out[{std::stoi(f[0]), std::stoull(f[2])}] = {std::stod(f[7]), std::stod(f[8])};
  }
  return out;
}

struct Row {
  int n = 0, K = 0;
  std::uint64_t seed = 0;
  double alns = 0, cg_ub = 0, cg_lb = NAN, lp = 0, runtime = 0;
  bool valid = false, converged = false, legal = true;
  int iterations = 0, ng = 0;
  std::size_t columns = 0;
  MilpRef milp;
};

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Column generation lower bounds (V1-T9)"};
  fs::path config, compare_csv, out = results_dir() / "cg";
  std::vector<int> orders;
  std::vector<std::uint64_t> seeds;
  double cg_time = 120, mip_time = 30;
  bool v0_rules = false;
  int ng = 8;
  app.add_option("--config", config, "size family (config/instance_scale.yaml)")->required()->check(CLI::ExistingFile);
  app.add_option("--orders", orders, "sizes (default: the family's)");
  app.add_option("--seeds", seeds, "seeds (default: the family's)");
  app.add_option("--cg-time", cg_time, "column generation time limit (s)");
  app.add_option("--mip-time", mip_time, "final MIP over the columns (s)");
  app.add_option("--ng", ng, "ng-neighbourhood size of the relaxed pricing");
  app.add_flag("--v0-rules", v0_rules, "V0 break rules (as in V1-T4)");
  app.add_option("--compare-csv", compare_csv, "V1-T4 compare.csv (MILP objective and bound, V0 rules)");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto sc = data::load_scale_config(config);
    if (orders.empty()) orders = sc.orders;
    if (seeds.empty()) seeds = sc.seeds;
    const Config rules = load_config();
    const auto milp = compare_csv.empty() ? std::map<std::pair<int, std::uint64_t>, MilpRef>{} : read_compare(compare_csv);
    fs::create_directories(out);
    const std::string tag = v0_rules ? "v0" : "v1";
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    std::ofstream csv(out / ("cg_" + tag + ".csv"), std::ios::binary | std::ios::trunc);
    csv << "n,K,seed,alns,cg_ub,cg_lb,lp,bound_valid,ng,converged,iterations,columns,runtime_s,milp,milp_bound,"
           "alns_gap_cg,alns_gap_milp,legal\n";
    std::vector<Row> rows;
    for (const int n : orders) {
      const double T = n <= 12 ? 5.0 : n <= 20 ? 15.0 : 45.0;  // as in V1-T4
      for (const auto seed : seeds) {
        const auto cw = week::generate_certified_week(data::scale_instance(sc, n), rules, seed);
        if (!cw) {
          std::cerr << "n=" << n << " seed=" << seed << ": no certified instance\n";
          continue;
        }
        DayInstance day = make_day_instance(cw->generated.week, 0);
        if (v0_rules) {
          day.rules.allow_short_break = false;
          day.rules.allow_split_break = false;
        }
        Row row;
        row.n = n;
        row.K = static_cast<int>(day.drivers.size());
        row.seed = seed;
        alns::Options ao;
        ao.time_limit_s = T;
        ao.seed = seed;
        ao.collect_pool = true;
        const auto a = alns::solve(day, ao);
        const auto ac = check::check_day(day, a.plan);
        row.alns = ac.objective;

        cg::Options co;
        co.time_limit_s = cg_time;
        co.mip_time_s = mip_time;
        co.pricing.ng = ng;
        co.initial = a.pool;
        co.incumbent = a.routes;
        const auto r = cg::solve(env, day, co);
        const auto plan = alns::make_plan(day, r.routes, r.bank);
        const auto rc = check::check_day(day, plan);
        row.cg_ub = rc.objective;
        row.cg_lb = r.bound_valid ? r.lower_bound : NAN;
        row.valid = r.bound_valid;
        row.lp = r.lp_value;
        row.converged = r.converged;
        row.iterations = r.iterations;
        row.columns = r.columns;
        row.runtime = r.runtime_s;
        row.ng = r.ng_used;
        row.legal = ac.ok() && rc.ok();
        if (const auto it = milp.find({n, seed}); it != milp.end() && v0_rules) row.milp = it->second;

        const double gap_cg = row.valid ? 100.0 * (row.alns - row.cg_lb) / row.alns : NAN;
        const double gap_milp = std::isnan(row.milp.bound) ? NAN : 100.0 * (row.alns - row.milp.bound) / row.alns;
        csv << n << ',' << row.K << ',' << seed << ',' << row.alns << ',' << row.cg_ub << ',' << row.cg_lb << ','
            << row.lp << ',' << (row.valid ? 1 : 0) << ',' << row.ng << ',' << (row.converged ? 1 : 0) << ','
            << row.iterations << ',' << row.columns << ',' << row.runtime << ',' << row.milp.objective << ','
            << row.milp.bound << ',' << gap_cg << ',' << gap_milp << ',' << (row.legal ? 1 : 0) << '\n';
        csv.flush();
        std::cout << "n=" << n << " seed=" << seed << ": ALNS " << fx(row.alns, 2) << ", CG UB " << fx(row.cg_ub, 2)
                  << ", LB " << fx(row.cg_lb, 2) << " (LP " << fx(row.lp, 2) << ", " << row.iterations << " it, "
                  << row.columns << " cols, " << fx(row.runtime) << " s, " << r.pricing_groups << " pricing groups, ng " << row.ng
                  << (row.converged ? ", converged" : "") << "), certified ALNS gap " << fx(gap_cg, 2) << " %"
                  << (std::isnan(gap_milp) ? "" : " (MILP bound: " + fx(gap_milp, 2) + " %)")
                  << (row.legal ? "" : " CHECKER VIOLATIONS") << std::endl;
        rows.push_back(row);
      }
    }

    std::ostringstream md;
    md << "# Column generation lower bounds (V1-T9), " << (v0_rules ? "V0" : "V1") << " break rules\n\n"
       << "Instances of V1-T4 (`" << config.filename().string() << "`, one certified day per size and seed). ALNS as "
          "in V1-T4; column generation " << fx(cg_time, 0) << " s from the ALNS route pool, ng = " << ng
       << ", final MIP " << fx(mip_time, 0) << " s. Certified gap = (ALNS - lower bound) / ALNS.\n\n"
       << "| n | K | runs | bound valid | converged | median CG time (s) | ALNS certified gap, CG bound (median / max)"
          " | ALNS certified gap, MILP-300 s bound (median / max) | CG upper bound <= ALNS |\n"
          "|---|---|---|---|---|---|---|---|---|\n";
    std::map<int, std::vector<Row>> by_n;
    for (const auto& r : rows) by_n[r.n].push_back(r);
    for (const auto& [n, rs] : by_n) {
      std::vector<double> gc, gm, rt;
      int valid = 0, conv = 0, ub_ok = 0;
      for (const auto& r : rs) {
        if (r.valid) {
          ++valid;
          gc.push_back(100.0 * (r.alns - r.cg_lb) / r.alns);
        }
        if (!std::isnan(r.milp.bound)) gm.push_back(100.0 * (r.alns - r.milp.bound) / r.alns);
        conv += r.converged ? 1 : 0;
        ub_ok += r.cg_ub <= r.alns + 1e-6 ? 1 : 0;
        rt.push_back(r.runtime);
      }
      auto mm = [&](const std::vector<double>& v) {
        return v.empty() ? std::string{"—"} : fx(median(v), 2) + " % / " + fx(*std::ranges::max_element(v), 2) + " %";
      };
      md << "| " << n << " | " << rs.front().K << " | " << rs.size() << " | " << valid << " | " << conv << " | "
         << fx(median(rt), 0) << " | " << mm(gc) << " | " << mm(gm) << " | " << ub_ok << "/" << rs.size() << " |\n";
    }
    data::write_text_file(out / ("cg_" + tag + ".md"), md.str());
    std::cout << md.str();
    return 0;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-cg: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-cg: " << e.what() << "\n";
    return 2;
  }
}
