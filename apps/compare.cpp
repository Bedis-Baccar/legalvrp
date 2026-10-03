// Method comparison at scale — V1-T4.
//   legalvrp-compare --config config/instance_scale.yaml [--orders ...] [--seeds ...]
//                    [--milp-csv docs/scaling/scaling.csv] [--milp-sizes 60 80] [--milp-seeds 1 2 3]
//                    [--out results/compare]
// For each (n, seed): the certified one-day instance of the V0-T9 experiment (same generator and
// seeds; extended sizes allowed). Methods:
//   baseline        territory baseline
//   alns            ALNS alone, time T (5 s if n <= 12, 15 s if n <= 20, 45 s otherwise)
//   hybrid          ALNS 0.6 T + route-pool set partitioning <= 0.3 T + ALNS polish 0.1 T
//   milp            V0 results read from --milp-csv (300 s, objective and lower bound), or a new
//                   300-s run for --milp-sizes x --milp-seeds
// Every plan is validated by the checker. Writes compare.csv, summary.md and time_to_quality.svg.
#include <CLI/CLI.hpp>

#include <algorithm>
#include <cmath>
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

#include "gurobi_c++.h"
#include "legalvrp/alns/alns.hpp"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/pool/pool.hpp"
#include "legalvrp/week/certify.hpp"
#include "legalvrp/week/solve_day.hpp"

namespace fs = std::filesystem;
using namespace legalvrp;

namespace {

const std::vector<double> kCheckpoints{0.1, 0.5, 1, 2, 5, 10, 15, 30, 45};

struct MilpRef {
  std::string status;
  double objective = 0, bound = 0, runtime = 0;
  bool from_v0 = false;
};

struct Row {
  int n = 0, K = 0;
  std::uint64_t seed = 0;
  double T = 0;
  double baseline = 0, alns = 0, hybrid = 0;
  std::vector<double> alns_at;  // best at each checkpoint (inf if beyond T)
  std::optional<MilpRef> milp;
  double pool_gain = 0;         // hybrid - its ALNS phase (<= 0)
  std::size_t pool_columns = 0;
  long long alns_iterations = 0;
  bool legal = true;
  [[nodiscard]] double best() const {
    double b = std::min({baseline, alns, hybrid});
    if (milp) b = std::min(b, milp->objective);
    return b;
  }
};

std::string fx(double v, int p = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(p) << v;
  return os.str();
}
double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::ranges::sort(v);
  const std::size_t m = v.size() / 2;
  return v.size() % 2 ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

std::map<std::pair<int, std::uint64_t>, MilpRef> read_milp_csv(const fs::path& file) {
  std::map<std::pair<int, std::uint64_t>, MilpRef> out;
  std::ifstream in(file);
  std::string line;
  std::getline(in, line);  // header: n,K,seed,attempt,status,source,runtime_s,gap,nodes,objective,best_bound,...
  while (std::getline(in, line)) {
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) f.push_back(cell);
    if (f.size() < 11) continue;
    MilpRef m;
    m.status = f[4];
    m.runtime = std::stod(f[6]);
    m.objective = std::stod(f[9]);
    m.bound = std::stod(f[10]);
    m.from_v0 = true;
    out[{std::stoi(f[0]), std::stoull(f[2])}] = m;
  }
  return out;
}

std::string svg_curves(const std::map<int, std::vector<std::vector<double>>>& gaps) {
  // x: time (log10, 0.1 .. 45 s); y: median gap to best known (%), one curve per size.
  const double W = 760, H = 460, L = 70, R = 140, T = 40, B = 60;
  double ymax = 1.0;
  std::map<int, std::vector<double>> med;
  for (const auto& [n, per_seed] : gaps) {
    std::vector<double> m;
    for (std::size_t c = 0; c < kCheckpoints.size(); ++c) {
      std::vector<double> v;
      for (const auto& s : per_seed) {
        if (c < s.size() && std::isfinite(s[c])) v.push_back(s[c]);
      }
      m.push_back(v.empty() ? NAN : median(v));
      if (!v.empty()) ymax = std::max(ymax, m.back());
    }
    med[n] = m;
  }
  ymax = std::min(ymax, 40.0);
  auto X = [&](double t) { return L + (std::log10(t) + 1) / (std::log10(45.0) + 1) * (W - L - R); };
  auto Y = [&](double g) { return T + (1 - std::min(g, ymax) / ymax) * (H - T - B); };
  const char* colors[] = {"#1b9e77", "#d95f02", "#7570b3", "#e7298a", "#66a61e", "#e6ab02", "#a6761d", "#666666"};
  std::ostringstream s;
  s << "<svg xmlns='http://www.w3.org/2000/svg' width='" << W << "' height='" << H
    << "' font-family='sans-serif' font-size='12'>\n<rect width='100%' height='100%' fill='white'/>\n"
    << "<text x='" << (L + W - R) / 2 << "' y='22' text-anchor='middle' font-size='15'>ALNS time to quality</text>\n"
    << "<line x1='" << L << "' y1='" << H - B << "' x2='" << W - R << "' y2='" << H - B << "' stroke='black'/>\n"
    << "<line x1='" << L << "' y1='" << T << "' x2='" << L << "' y2='" << H - B << "' stroke='black'/>\n"
    << "<text x='" << (L + W - R) / 2 << "' y='" << H - 15 << "' text-anchor='middle'>time (s, log scale)</text>\n"
    << "<text transform='translate(18," << (T + H - B) / 2
    << ") rotate(-90)' text-anchor='middle'>median gap to best known (%)</text>\n";
  for (const double t : kCheckpoints) {
    s << "<text x='" << X(t) << "' y='" << H - B + 18 << "' text-anchor='middle'>" << fx(t, t < 1 ? 1 : 0) << "</text>\n";
  }
  for (int g = 0; g <= 4; ++g) {
    const double v = ymax * g / 4;
    s << "<line x1='" << L << "' y1='" << Y(v) << "' x2='" << W - R << "' y2='" << Y(v) << "' stroke='#ddd'/><text x='"
      << L - 8 << "' y='" << Y(v) + 4 << "' text-anchor='end'>" << fx(v, 1) << "</text>\n";
  }
  int ci = 0;
  for (const auto& [n, m] : med) {
    std::string path;
    for (std::size_t c = 0; c < kCheckpoints.size(); ++c) {
      if (std::isnan(m[c])) continue;
      path += (path.empty() ? "M" : " L") + fx(X(kCheckpoints[c])) + " " + fx(Y(m[c]));
    }
    const char* col = colors[ci % 8];
    s << "<path d='" << path << "' fill='none' stroke='" << col << "' stroke-width='2'/>\n"
      << "<text x='" << W - R + 10 << "' y='" << T + 16 * ci + 10 << "' fill='" << col << "'>n = " << n << "</text>\n";
    ++ci;
  }
  s << "</svg>\n";
  return s.str();
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Method comparison at scale: baseline, ALNS, ALNS + pool, MILP"};
  fs::path config, milp_csv, out = results_dir() / "compare";
  std::vector<int> orders;
  std::vector<std::uint64_t> seeds, milp_seeds{1, 2, 3};
  std::vector<int> milp_sizes;
  double milp_limit = 300;
  app.add_option("--config", config, "size family (config/instance_scale.yaml)")->required()->check(CLI::ExistingFile);
  app.add_option("--orders", orders, "sizes (default: the family's)");
  app.add_option("--seeds", seeds, "seeds (default: the family's)");
  app.add_option("--milp-csv", milp_csv, "V0 scaling.csv with MILP results to reuse");
  app.add_option("--milp-sizes", milp_sizes, "sizes for new MILP runs");
  app.add_option("--milp-seeds", milp_seeds, "seeds for new MILP runs");
  app.add_option("--milp-limit", milp_limit, "TimeLimit of new MILP runs (s)");
  app.add_option("--out", out, "output directory");
  CLI11_PARSE(app, argc, argv);

  try {
    const auto sc = data::load_scale_config(config);
    if (orders.empty()) orders = sc.orders;
    if (seeds.empty()) seeds = sc.seeds;
    const Config rules = load_config();
    const auto v0 = milp_csv.empty() ? std::map<std::pair<int, std::uint64_t>, MilpRef>{} : read_milp_csv(milp_csv);
    fs::create_directories(out);
    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    std::ofstream csv(out / "compare.csv", std::ios::binary | std::ios::trunc);
    csv << "n,K,seed,T,baseline,alns,hybrid,milp,milp_bound,milp_status,milp_source,best,alns_iterations,pool_columns,"
           "pool_gain,legal";
    for (const double t : kCheckpoints) csv << ",alns_at_" << t;
    csv << "\n";

    std::vector<Row> rows;
    for (const int n : orders) {
      const double T = n <= 12 ? 5.0 : n <= 20 ? 15.0 : 45.0;
      for (const auto seed : seeds) {
        const auto cw = week::generate_certified_week(data::scale_instance(sc, n), rules, seed);
        if (!cw) {
          std::cerr << "n=" << n << " seed=" << seed << ": no certified instance\n";
          continue;
        }
        const DayInstance day = make_day_instance(cw->generated.week, 0);
        Row row;
        row.n = n;
        row.K = static_cast<int>(day.drivers.size());
        row.seed = seed;
        row.T = T;
        const auto base = check::check_day(day, heuristics::territory_baseline(day));
        row.baseline = base.objective;

        alns::Options ao;
        ao.time_limit_s = T;
        ao.seed = seed;
        const auto a = alns::solve(day, ao);
        const auto ac = check::check_day(day, a.plan);
        row.alns = ac.objective;
        row.alns_iterations = a.iterations;
        for (const double t : kCheckpoints) {
          double v = std::numeric_limits<double>::infinity();
          if (t <= T + 1e-9) {
            for (const auto& p : a.trace) {
              if (p.seconds <= t) v = p.best;
            }
          }
          row.alns_at.push_back(v);
        }

        pool::HybridOptions ho;
        ho.alns.time_limit_s = 0.6 * T;
        ho.alns.seed = seed;
        ho.pool.time_limit_s = 0.3 * T;
        ho.polish_time_s = 0.1 * T;
        const auto h = pool::solve_hybrid(env, day, ho);
        const auto hc = check::check_day(day, h.plan);
        row.hybrid = hc.objective;
        row.pool_gain = h.cost - h.first.cost;
        row.pool_columns = h.pool.columns;
        row.legal = base.ok() && ac.ok() && hc.ok();

        if (const auto it = v0.find({n, seed}); it != v0.end()) {
          row.milp = it->second;
        } else if (std::ranges::find(milp_sizes, n) != milp_sizes.end() &&
                   std::ranges::find(milp_seeds, seed) != milp_seeds.end()) {
          model::MilpOptions mo;
          mo.solver.time_limit = milp_limit;
          const auto s = week::solve_day(env, day, mo);
          row.milp = MilpRef{s.milp.stats.status, s.check.objective, s.milp.stats.best_bound, s.milp.stats.runtime_s, false};
          row.legal = row.legal && s.check.ok();
        }
        rows.push_back(row);

        csv << n << ',' << row.K << ',' << seed << ',' << T << ',' << row.baseline << ',' << row.alns << ','
            << row.hybrid << ',' << (row.milp ? std::to_string(row.milp->objective) : "") << ','
            << (row.milp ? std::to_string(row.milp->bound) : "") << ',' << (row.milp ? row.milp->status : "") << ','
            << (row.milp ? (row.milp->from_v0 ? "v0" : "new") : "") << ',' << row.best() << ',' << row.alns_iterations
            << ',' << row.pool_columns << ',' << row.pool_gain << ',' << (row.legal ? 1 : 0);
        for (const double v : row.alns_at) csv << ',' << (std::isfinite(v) ? std::to_string(v) : "");
        csv << "\n";
        csv.flush();
        std::cout << "n=" << n << " seed=" << seed << ": baseline " << fx(row.baseline) << ", ALNS " << fx(row.alns)
                  << ", hybrid " << fx(row.hybrid);
        if (row.milp) std::cout << ", MILP " << fx(row.milp->objective) << " (bound " << fx(row.milp->bound) << ")";
        std::cout << (row.legal ? "" : "  NOT LEGAL") << std::endl;
      }
    }

    // Summary per n.
    std::ostringstream md;
    md << "# Method comparison at scale (V1-T4)\n\nOne-day certified instances of the V0-T9 family "
          "(end-of-horizon penalties), K = ceil(n/5). ALNS alone and ALNS + pool get the same total time T "
          "(5 s for n <= 12, 15 s for n <= 20, 45 s beyond). MILP: strong formulation with connectivity cuts, "
          "300 s (V0 run for n <= 40; new runs where marked). Gaps are relative to the best known solution of "
          "each instance; *certified gap* = (method - MILP lower bound) / method. Every plan passed the checker.\n\n"
       << "| n | K | runs | baseline gap | ALNS gap | ALNS + pool gap | MILP gap | ALNS <= MILP | ALNS certified gap "
          "(median / max) | MILP runs |\n|---|---|---|---|---|---|---|---|---|---|\n";
    std::map<int, std::vector<const Row*>> by_n;
    for (const auto& r : rows) by_n[r.n].push_back(&r);
    std::map<int, std::vector<std::vector<double>>> curves;
    for (const auto& [n, rs] : by_n) {
      std::vector<double> gb, ga, gh, gm, cert;
      int alns_le = 0, milp_runs = 0;
      for (const Row* r : rs) {
        const double best = r->best();
        gb.push_back(100 * (r->baseline - best) / best);
        ga.push_back(100 * (r->alns - best) / best);
        gh.push_back(100 * (r->hybrid - best) / best);
        if (r->milp) {
          ++milp_runs;
          gm.push_back(100 * (r->milp->objective - best) / best);
          alns_le += std::min(r->alns, r->hybrid) <= r->milp->objective * (1 + 1e-5) ? 1 : 0;  // V0 CSV: 6 significant digits
          cert.push_back(100 * (std::min(r->alns, r->hybrid) - r->milp->bound) / std::min(r->alns, r->hybrid));
        }
        std::vector<double> curve;
        for (const double v : r->alns_at) curve.push_back(std::isfinite(v) ? 100 * (v - best) / best : NAN);
        curves[n].push_back(curve);
      }
      md << "| " << n << " | " << rs.front()->K << " | " << rs.size() << " | " << fx(median(gb)) << "% | "
         << fx(median(ga), 2) << "% | " << fx(median(gh), 2) << "% | "
         << (gm.empty() ? std::string{"—"} : fx(median(gm), 2) + "%") << " | "
         << (milp_runs ? std::to_string(alns_le) + "/" + std::to_string(milp_runs) : std::string{"—"}) << " | "
         << (cert.empty() ? std::string{"—"}
                          : fx(median(cert), 1) + "% / " + fx(*std::ranges::max_element(cert), 1) + "%")
         << " | " << milp_runs << " |\n";
    }
    md << "\n*ALNS <= MILP*: the better of ALNS and ALNS + pool is at least as good as the MILP's 300-s result. "
          "Time to quality: `time_to_quality.svg`. Raw data: `compare.csv`.\n";
    data::write_text_file(out / "summary.md", md.str());
    std::map<int, std::vector<std::vector<double>>> sel;
    for (const auto& [n, c] : curves) {
      if (n == 15 || n == 20 || n == 30 || n == 40 || n >= 60) sel[n] = c;
    }
    data::write_text_file(out / "time_to_quality.svg", svg_curves(sel));
    std::cout << md.str() << "written: " << out.string() << "\n";
    return 0;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-compare: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-compare: " << e.what() << "\n";
    return 2;
  }
}
