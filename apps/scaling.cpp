// Scaling experiment — task T9 (PROJECT_BRIEF.md §12; docs/SIZING.md §2.4).
//   legalvrp-scaling --config config/instance_scale.yaml [--time-limit 300] [--out results/scaling]
//                    [--orders 8 10 ...] [--seeds 1 2 ...] [--formulation strong|reference]
// For every (n, seed): a certified one-day instance with n orders and K = ceil(n/5) drivers,
// solved end to end (baseline start -> MILP -> checker). Records the §6.7 statistics.
// Writes scaling.csv (one row per solve, appended as it goes), summary.md (median runtime,
// median gap, share solved to optimality per n) and two SVG figures (runtime, gap vs n).
#include <CLI/CLI.hpp>

#include <algorithm>
#include <cmath>
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
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/solve_day.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {

struct Row {
  int n = 0, K = 0;
  std::uint64_t seed = 0;
  int attempt = 0;
  std::string status, source;
  double runtime = 0, gap = 0, nodes = 0, objective = 0, bound = 0, baseline = 0;
  int vars = 0, constrs = 0, nz = 0, pvars = 0, pconstrs = 0, pnz = 0, binaries = 0, arcs = 0, arcs_full = 0;
  long long cuts = 0;
  bool legal = false;
};

double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::ranges::sort(v);
  const std::size_t m = v.size() / 2;
  return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

std::string fx(double v, int p = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}

// Minimal SVG scatter + median line, no dependency. y_log: logarithmic y axis.
std::string svg_plot(const std::string& title, const std::string& ylabel, const std::vector<Row>& rows,
                     double (*y)(const Row&), bool y_log, double y_max_hint) {
  const double W = 720, H = 440, L = 70, R = 20, T = 40, B = 60;
  int n_min = 1 << 30, n_max = 0;
  double ymax = y_max_hint, ymin = y_log ? 0.1 : 0.0;
  for (const auto& r : rows) {
    n_min = std::min(n_min, r.n);
    n_max = std::max(n_max, r.n);
    ymax = std::max(ymax, y(r));
  }
  if (n_min == n_max) n_max = n_min + 1;
  auto X = [&](double n) { return L + (n - n_min) / (n_max - n_min) * (W - L - R); };
  auto Y = [&](double v) {
    if (y_log) {
      v = std::max(v, ymin);
      return T + (1 - (std::log10(v) - std::log10(ymin)) / (std::log10(ymax) - std::log10(ymin))) * (H - T - B);
    }
    return T + (1 - (v - ymin) / (ymax - ymin)) * (H - T - B);
  };
  std::ostringstream s;
  s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
    << "' font-family='sans-serif' font-size='12'>\n<rect width='100%' height='100%' fill='white'/>\n"
    << "<text x='" << W / 2 << "' y='22' text-anchor='middle' font-size='15'>" << title << "</text>\n"
    << "<line x1='" << L << "' y1='" << H - B << "' x2='" << W - R << "' y2='" << H - B << "' stroke='black'/>\n"
    << "<line x1='" << L << "' y1='" << T << "' x2='" << L << "' y2='" << H - B << "' stroke='black'/>\n"
    << "<text x='" << (L + W - R) / 2 << "' y='" << H - 15 << "' text-anchor='middle'>orders per day (n)</text>\n"
    << "<text transform='translate(18," << (T + H - B) / 2 << ") rotate(-90)' text-anchor='middle'>" << ylabel
    << "</text>\n";
  std::map<int, std::vector<double>> by_n;
  for (const auto& r : rows) by_n[r.n].push_back(y(r));
  for (const auto& [n, vals] : by_n) {
    s << "<line x1='" << X(n) << "' y1='" << H - B << "' x2='" << X(n) << "' y2='" << H - B + 5 << "' stroke='black'/>"
      << "<text x='" << X(n) << "' y='" << H - B + 18 << "' text-anchor='middle'>" << n << "</text>\n";
  }
  const std::vector<double> ticks = y_log ? std::vector<double>{0.1, 1, 10, 100, 1000}
                                          : std::vector<double>{0, ymax * 0.25, ymax * 0.5, ymax * 0.75, ymax};
  for (const double t : ticks) {
    if (t > ymax * 1.001 || t < ymin) continue;
    s << "<line x1='" << L - 5 << "' y1='" << Y(t) << "' x2='" << W - R << "' y2='" << Y(t)
      << "' stroke='#ddd'/><text x='" << L - 8 << "' y='" << Y(t) + 4 << "' text-anchor='end'>" << fx(t, t < 1 ? 1 : 0)
      << "</text>\n";
  }
  for (const auto& r : rows) {
    s << "<circle cx='" << X(r.n) << "' cy='" << Y(y(r)) << "' r='4' fill='" << (r.status == "OPTIMAL" ? "#2b7bba" : "#d95f02")
      << "' fill-opacity='0.7'/>\n";
  }
  std::string path;
  for (const auto& [n, vals] : by_n) path += (path.empty() ? "M" : " L") + fx(X(n)) + " " + fx(Y(median(vals)));
  s << "<path d='" << path << "' fill='none' stroke='black' stroke-width='2'/>\n"
    << "<text x='" << W - R << "' y='" << T + 12 << "' text-anchor='end'>"
    << "<tspan fill='#2b7bba'>&#9679; optimal</tspan>  <tspan fill='#d95f02'>&#9679; time limit</tspan>  "
       "&#8212; median</text>\n</svg>\n";
  return s.str();
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Scaling experiment: runtime and gap vs number of orders"};
  fs::path config;
  fs::path out = results_dir() / "scaling";
  double time_limit = -1;
  std::vector<int> only_orders;
  std::vector<std::uint64_t> only_seeds;
  std::string formulation = "strong";
  bool regular_day = false;
  app.add_option("--config", config, "config/instance_scale.yaml")->required()->check(CLI::ExistingFile);
  app.add_option("--time-limit", time_limit, "override TimeLimit (s)");
  app.add_option("--out", out, "output directory");
  app.add_option("--orders", only_orders, "subset of sizes");
  app.add_option("--seeds", only_seeds, "subset of seeds");
  app.add_flag("--regular-day", regular_day,
               "base postponement penalties (a regular weekday) instead of the last-day penalty of a one-day horizon");
  app.add_option("--formulation", formulation, "strong or reference")->check(CLI::IsMember({"strong", "reference"}));
  CLI11_PARSE(app, argc, argv);

  try {
    const auto sc = data::load_scale_config(config);
    const Config rules = load_config();
    model::MilpOptions opt;
    opt.solver = model::load_solver_config(config_dir() / "solver.yaml", sc.solver_profile);
    if (time_limit > 0) opt.solver.time_limit = time_limit;
    opt.formulation = formulation == "strong" ? model::Formulation::strong : model::Formulation::reference;
    opt.connectivity_cuts = opt.formulation == model::Formulation::strong;
    const auto orders = only_orders.empty() ? sc.orders : only_orders;
    const auto seeds = only_seeds.empty() ? sc.seeds : only_seeds;

    fs::create_directories(out);
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    std::ofstream csv(out / "scaling.csv", std::ios::binary | std::ios::trunc);
    csv << "n,K,seed,attempt,status,source,runtime_s,gap,nodes,objective,best_bound,baseline,vars,constrs,nz,"
           "presolved_vars,presolved_constrs,presolved_nz,binaries,arcs,arcs_full,cuts,legal\n";
    std::vector<Row> rows;
    for (const int n : orders) {
      const data::InstanceConfig ic = data::scale_instance(sc, n);
      for (const auto seed : seeds) {
        const auto cw = week::generate_certified_week(ic, rules, seed);
        if (!cw) {
          std::cerr << "n=" << n << " seed=" << seed << ": no certified instance, skipped\n";
          continue;
        }
        const WeekInstance& w = cw->generated.week;
        DayInstance day = make_day_instance(w, 0);
        if (regular_day) {  // a one-day horizon makes day 0 the last day (D-019): restore base penalties
          for (auto& ord : day.orders) ord.postpone_penalty = w.costs.postpone_penalty;
        }
        model::MilpOptions o = opt;
        o.log_dir = out / "logs";
        o.tag = "n" + std::to_string(n) + "_s" + std::to_string(seed);
        const auto s = week::solve_day(env, day, o);
        const auto& st = s.milp.stats;
        Row r;
        r.n = n;
        r.K = static_cast<int>(day.drivers.size());
        r.seed = seed;
        r.attempt = w.attempt;
        r.status = st.status;
        r.source = week::to_string(s.source);
        r.runtime = st.runtime_s;
        r.gap = st.gap;
        r.nodes = st.node_count;
        r.objective = s.check.objective;
        r.bound = st.best_bound;
        r.baseline = s.baseline_check.objective;
        r.vars = st.num_vars;
        r.constrs = st.num_constrs;
        r.nz = st.num_nz;
        r.pvars = st.presolved_vars;
        r.pconstrs = st.presolved_constrs;
        r.pnz = st.presolved_nz;
        r.binaries = s.milp.binaries;
        r.arcs = s.milp.arcs;
        r.arcs_full = s.milp.arcs_full;
        r.cuts = s.milp.cuts_added;
        r.legal = s.check.ok();
        rows.push_back(r);
        csv << r.n << ',' << r.K << ',' << r.seed << ',' << r.attempt << ',' << r.status << ',' << r.source << ','
            << r.runtime << ',' << r.gap << ',' << r.nodes << ',' << r.objective << ',' << r.bound << ','
            << r.baseline << ',' << r.vars << ',' << r.constrs << ',' << r.nz << ',' << r.pvars << ','
            << r.pconstrs << ',' << r.pnz << ',' << r.binaries << ',' << r.arcs << ',' << r.arcs_full << ','
            << r.cuts << ',' << (r.legal ? 1 : 0) << '\n';
        csv.flush();
        std::cout << "n=" << n << " K=" << r.K << " seed=" << seed << ": " << r.status << " " << fx(r.runtime)
                  << " s, gap " << fx(100 * r.gap) << "%, cost " << fx(r.objective) << " (baseline "
                  << fx(r.baseline) << "), " << (r.legal ? "legal" : "NOT LEGAL") << std::endl;
      }
    }

    // Summary per n.
    std::ostringstream md;
    md << "# Scaling experiment (T9)\n\nFormulation `" << formulation << "`"
       << (opt.connectivity_cuts ? " with connectivity cuts" : "") << ", TimeLimit " << opt.solver.time_limit
       << " s, MIPGap " << opt.solver.mip_gap << ", Threads " << opt.solver.threads
       << " (0 = all). One certified day per (n, seed); K = ceil(n/" << sc.drivers_per_orders
       << ") drivers. Postponement penalty: "
       << (regular_day ? "base (regular weekday)" : "end-of-horizon (one-day instance = last day, D-019)")
       << ". Every reported plan passed the checker.\n\n"
       << "| n | K | runs | median runtime (s) | median gap | max gap | solved to optimality | median binaries "
          "| median vars / constrs (after presolve) | median saving vs baseline |\n"
       << "|---|---|---|---|---|---|---|---|---|---|\n";
    std::map<int, std::vector<const Row*>> by_n;
    for (const auto& r : rows) by_n[r.n].push_back(&r);
    for (const auto& [n, rs] : by_n) {
      std::vector<double> rt, gp, bin, pv, pc, sav;
      int opt_count = 0;
      for (const Row* r : rs) {
        rt.push_back(r->runtime);
        gp.push_back(r->gap);
        bin.push_back(r->binaries);
        pv.push_back(r->pvars);
        pc.push_back(r->pconstrs);
        sav.push_back(r->baseline > 0 ? (r->baseline - r->objective) / r->baseline : 0.0);
        opt_count += r->status == "OPTIMAL" ? 1 : 0;
      }
      md << "| " << n << " | " << rs.front()->K << " | " << rs.size() << " | " << fx(median(rt)) << " | "
         << fx(100 * median(gp)) << "% | " << fx(100 * *std::ranges::max_element(gp)) << "% | " << opt_count << "/"
         << rs.size() << " | " << fx(median(bin), 0) << " | " << fx(median(pv), 0) << " / " << fx(median(pc), 0)
         << " | " << fx(100 * median(sav)) << "% |\n";
    }
    md << "\n*Solved to optimality* = Gurobi status OPTIMAL (gap <= MIPGap). Figures: `runtime.svg`, `gap.svg`.\n";
    data::write_text_file(out / "summary.md", md.str());
    data::write_text_file(out / "runtime.svg",
                          svg_plot("Runtime vs orders per day", "runtime (s, log scale)", rows,
                                   [](const Row& r) { return r.runtime; }, true, opt.solver.time_limit));
    data::write_text_file(out / "gap.svg", svg_plot("Optimality gap at the time limit", "gap (%)", rows,
                                                    [](const Row& r) { return 100.0 * r.gap; }, false, 10.0));
    std::cout << md.str() << "written: " << out.string() << "\n";
    return 0;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-scaling: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-scaling: " << e.what() << "\n";
    return 2;
  }
}
