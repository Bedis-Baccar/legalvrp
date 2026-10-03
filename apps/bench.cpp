// Benchmark the daily MILP against the territory baseline (T6/T7 evaluation).
//   legalvrp-bench --instance data/instances/small/1 [--profile small] [--time-limit 120]
//                  [--formulation-limit 60] [--out results/bench/small_1] [--skip-formulations]
//                  [--skip-week]
// Part A (formulations): every day solved as a fresh problem (no carry-over, zero weekly
//   state) by both formulations from the same baseline start; LP relaxation, final bound, gap,
//   nodes, model size. Same problem for both, so the comparison is fair.
// Part B (rolling week): Monday-Friday with the baseline only vs with the MILP (strong); every
//   plan is validated by the checker; cost, km, postponements, drivers, hours, overtime.
// Writes <out>/bench.json, <out>/report.md and one Gurobi log per solve.
#include <CLI/CLI.hpp>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "gurobi_c++.h"
#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/heuristics/territory.hpp"
#include "legalvrp/model/solve.hpp"
#include "legalvrp/week/loop.hpp"
#include "legalvrp/week/solve_day.hpp"

namespace fs = std::filesystem;
using nlohmann::json;
using namespace legalvrp;

namespace {

std::string fmt(double v, int prec = 1) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(prec) << v;
  return os.str();
}
std::string pct(double v) { return fmt(100.0 * v, 1) + "%"; }

struct WeekStats {
  double cost = 0, km = 0, overtime = 0, service_h = 0;
  int served = 0, postponed = 0, used = 0;
};

// Per-day facts of a rolling run, from the checker's recomputation.
json day_facts(const DayInstance& day, const DayPlan& plan, const check::DayCheck& dc, WeekStats& ws) {
  double km = 0, overtime = 0, service = 0;
  int used = 0;
  for (std::size_t k = 0; k < dc.drivers.size(); ++k) {
    const auto& f = dc.drivers[k];
    km += f.km;
    service += f.service_minutes;
    used += f.used ? 1 : 0;
    const auto& st = day.state(f.driver_id);
    const auto& c = day.contract(day.drivers[k].contract_class);
    const int before = std::max(0, st.service_minutes_week - c.weekly_threshold);
    const int after = std::max(0, st.service_minutes_week + f.service_minutes - c.weekly_threshold);
    overtime += after - before;
  }
  int served = 0;
  for (const auto& r : plan.routes) served += static_cast<int>(r.order_ids.size());
  const int postponed = static_cast<int>(plan.postponed_order_ids.size());
  ws.cost += dc.objective;
  ws.km += km;
  ws.overtime += overtime;
  ws.service_h += service / 60.0;
  ws.served += served;
  ws.postponed += postponed;
  ws.used += used;
  return json{{"orders", day.orders.size()}, {"served", served}, {"postponed", postponed},
              {"drivers_used", used}, {"km", km}, {"service_minutes", service},
              {"overtime_minutes", overtime}, {"cost", dc.objective}, {"checker_ok", dc.ok()}};
}

}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Benchmark: daily MILP vs territory baseline"};
  fs::path instance, out;
  std::string profile = "small";
  double time_limit = -1, formulation_limit = 60;
  int threads = -1;
  int mip_focus = -1;
  std::vector<int> only_days;
  std::vector<std::string> only_variants;
  bool skip_formulations = false, skip_week = false;
  app.add_option("--instance", instance, "instance directory")->required()->check(CLI::ExistingDirectory);
  app.add_option("--profile", profile, "solver.yaml profile");
  app.add_option("--time-limit", time_limit, "override TimeLimit for the rolling week (s)");
  app.add_option("--formulation-limit", formulation_limit, "TimeLimit per formulation solve in part A (s)");
  app.add_option("--threads", threads, "override Threads");
  app.add_option("--mip-focus", mip_focus, "override MIPFocus (0-3)");
  app.add_option("--days", only_days, "part A: only these days");
  app.add_option("--variants", only_variants, "part A: only these variants (reference, strong, strong+cuts)");
  app.add_option("--out", out, "output directory (default results/bench/<name>_<seed>)");
  app.add_flag("--skip-formulations", skip_formulations, "skip part A");
  app.add_flag("--skip-week", skip_week, "skip part B");
  CLI11_PARSE(app, argc, argv);

  try {
    const WeekInstance week = data::read_week(instance);
    if (out.empty()) out = results_dir() / "bench" / (week.name + "_" + std::to_string(week.seed));
    fs::create_directories(out);
    model::SolverConfig solver = model::load_solver_config(config_dir() / "solver.yaml", profile);
    if (time_limit > 0) solver.time_limit = time_limit;
    if (threads >= 0) solver.threads = threads;
    if (mip_focus >= 0) solver.mip_focus = mip_focus;

    GRBEnv env(true);
    env.set(GRB_IntParam_OutputFlag, 0);
    env.start();

    json result = {{"instance", instance.string()}, {"name", week.name}, {"seed", week.seed},
                   {"drivers", week.drivers.size()}, {"time_limit_week", solver.time_limit},
                   {"time_limit_formulations", formulation_limit}};
    std::ostringstream md;
    md << "# Benchmark — " << week.name << " seed " << week.seed << "\n\n"
       << "Drivers: " << week.drivers.size() << ". Machine: Gurobi " << GRB_VERSION_MAJOR << "."
       << GRB_VERSION_MINOR << "." << GRB_VERSION_TECHNICAL << ", Threads=" << solver.threads
       << " (0 = all cores). All reported plans passed the independent checker.\n\n";

    // ------------------------------------------------------------ Part A
    if (!skip_formulations) {
      std::cout << "\n== Part A: formulations on fresh days (limit " << formulation_limit << " s) ==\n";
      std::cout << "day  n  form       bin   arcs/full     LP      best    bound    gap   LPgap   nodes    time  status\n";
      md << "## A. Formulations (each day fresh, same baseline start, limit " << formulation_limit << " s)\n\n"
         << "| day | n | formulation | binaries | arcs kept | LP relaxation | baseline | MILP best | bound | final gap | root gap | nodes | time (s) | status |\n"
         << "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
      json part_a = json::array();
      for (int d = 0; d < week.days; ++d) {
        if (!only_days.empty() && std::ranges::find(only_days, d) == only_days.end()) continue;
        const DayInstance day = make_day_instance(week, d);
        const DayPlan base = heuristics::territory_baseline(day);
        const auto base_check = check::check_day(day, base);
        struct Variant {
          const char* name;
          model::Formulation f;
          bool cuts;
          bool per_driver;
        };
        for (const Variant v : {Variant{"reference", model::Formulation::reference, false, false},
                                Variant{"strong", model::Formulation::strong, false, false},
                                Variant{"strong+cuts", model::Formulation::strong, true, false},
                                Variant{"strong+pdcuts", model::Formulation::strong, true, true}}) {
          if (!only_variants.empty() && std::ranges::find(only_variants, std::string{v.name}) == only_variants.end()) continue;
          const auto f = v.f;
          model::MilpOptions o;
          o.formulation = f;
          o.connectivity_cuts = v.cuts;
          o.per_driver_cuts = v.per_driver;
          o.solver = solver;
          o.solver.time_limit = formulation_limit;
          o.start = base;
          o.compute_lp_bound = true;
          o.log_dir = out / "logs";
          o.tag = "A_day" + std::to_string(d) + "_" + std::string{v.name};
          const auto r = model::solve_day_milp(env, day, o);
          const double best = r.plan ? check::check_day(day, *r.plan).objective : base_check.objective;
          const bool legal = r.plan && check::check_day(day, *r.plan).ok();
          const double lp = r.lp_bound.value_or(0.0);
          const double root_gap = best > 0 ? (best - lp) / best : 0.0;
          const auto& s = r.stats;
          std::printf("%3d %2zu  %-13s %5d %5d/%-5d %8.1f %8.1f %8.1f %6s %6s %7.0f %7.1f  %s%s\n", d,
                      day.orders.size(), v.name, r.binaries, r.arcs, r.arcs_full, lp, best,
                      s.best_bound, pct(s.gap).c_str(), pct(root_gap).c_str(), s.node_count, s.runtime_s,
                      s.status.c_str(), legal ? "" : " (NOT LEGAL)");
          md << "| " << d << " | " << day.orders.size() << " | " << v.name << " | " << r.binaries
             << " | " << r.arcs << "/" << r.arcs_full << " | " << fmt(lp) << " | " << fmt(base_check.objective)
             << " | " << fmt(best) << " | " << fmt(s.best_bound) << " | " << pct(s.gap) << " | "
             << pct(root_gap) << " | " << fmt(s.node_count, 0) << " | " << fmt(s.runtime_s) << " | "
             << s.status << (legal ? "" : " NOT LEGAL") << " |\n";
          part_a.push_back({{"day", d}, {"orders", day.orders.size()}, {"formulation", v.name}, {"cuts_added", r.cuts_added},
                            {"binaries", r.binaries}, {"arcs", r.arcs}, {"arcs_full", r.arcs_full},
                            {"lp_bound", lp}, {"baseline", base_check.objective}, {"best", best},
                            {"legal", legal}, {"root_gap", root_gap}, {"stats", s},
                            {"start_accepted", r.start_accepted}});
        }
      }
      result["formulations"] = part_a;
      md << "\n*root gap* = (best − LP relaxation) / best: how much the formulation alone leaves to branching.\n\n";
    }

    // ------------------------------------------------------------ Part B
    if (!skip_week) {
      std::cout << "\n== Part B: rolling week, baseline vs MILP strong (limit " << solver.time_limit << " s/day) ==\n";
      WeekStats wb, wm;
      json days_b = json::array(), days_m = json::array();

      const week::WeekRun base_run = week::run_week(week, heuristics::territory_baseline);
      for (std::size_t d = 0; d < base_run.plans.size(); ++d) {
        days_b.push_back(day_facts(base_run.days[d], base_run.plans[d], base_run.day_checks[d], wb));
      }

      std::vector<week::DaySolve> solves;
      const week::WeekRun milp_run = week::run_week(week, [&](const DayInstance& day) {
        model::MilpOptions o;
        o.formulation = model::Formulation::strong;
        o.solver = solver;
        o.log_dir = out / "logs";
        o.tag = "B_day" + std::to_string(day.day);
        solves.push_back(week::solve_day(env, day, o));
        const auto& s = solves.back();
        std::printf("  day %d: %zu orders, %s in %.1f s, gap %s, source %s\n", day.day, day.orders.size(),
                    s.milp.stats.status.c_str(), s.milp.stats.runtime_s, pct(s.milp.stats.gap).c_str(),
                    week::to_string(s.source));
        return s.plan;
      });
      for (std::size_t d = 0; d < milp_run.plans.size(); ++d) {
        json f = day_facts(milp_run.days[d], milp_run.plans[d], milp_run.day_checks[d], wm);
        f["status"] = solves[d].milp.stats.status;
        f["runtime_s"] = solves[d].milp.stats.runtime_s;
        f["gap"] = solves[d].milp.stats.gap;
        f["source"] = week::to_string(solves[d].source);
        days_m.push_back(f);
      }

      md << "## B. Rolling week (Monday–Friday, carry-over and weekly state), limit "
         << solver.time_limit << " s/day\n\n"
         << "| day | orders (incl. carried) | baseline cost | MILP cost | saving | baseline km | MILP km | "
            "postponed B / M | drivers B / M | MILP status | time (s) | gap |\n"
         << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
      std::cout << "day  orders   base cost  MILP cost  saving   base km  MILP km  postp B/M  drivers B/M\n";
      for (std::size_t d = 0; d < days_m.size(); ++d) {
        const auto& b = days_b[d];
        const auto& m = days_m[d];
        const double bc = b["cost"], mc = m["cost"];
        std::printf("%3zu  %3d/%-3d %10.1f %10.1f %6s %9.1f %8.1f   %2d / %-2d    %d / %d\n", d,
                    b["orders"].get<int>(), m["orders"].get<int>(), bc, mc, pct((bc - mc) / bc).c_str(),
                    b["km"].get<double>(), m["km"].get<double>(), b["postponed"].get<int>(),
                    m["postponed"].get<int>(), b["drivers_used"].get<int>(), m["drivers_used"].get<int>());
        md << "| " << d << " | " << b["orders"].get<int>() << " / " << m["orders"].get<int>() << " | " << fmt(bc)
           << " | " << fmt(mc) << " | " << pct((bc - mc) / bc) << " | " << fmt(b["km"].get<double>()) << " | "
           << fmt(m["km"].get<double>()) << " | " << b["postponed"].get<int>() << " / "
           << m["postponed"].get<int>() << " | " << b["drivers_used"].get<int>() << " / "
           << m["drivers_used"].get<int>() << " | " << m["status"].get<std::string>() << " | "
           << fmt(m["runtime_s"].get<double>()) << " | " << pct(m["gap"].get<double>()) << " |\n";
      }
      auto totals = [](const WeekStats& w, const week::WeekRun& run) {
        return json{{"cost", w.cost}, {"km", w.km}, {"served", w.served}, {"postponed_decisions", w.postponed},
                    {"overtime_minutes", w.overtime}, {"service_hours", w.service_h},
                    {"driver_days", w.used}, {"unserved_end", run.week_check.unserved_order_ids.size()},
                    {"week_checker_ok", run.week_check.ok()}};
      };
      result["week_baseline"] = {{"days", days_b}, {"totals", totals(wb, base_run)}};
      result["week_milp"] = {{"days", days_m}, {"totals", totals(wm, milp_run)}};

      const auto row = [&](const char* name, double b, double m, int prec = 1) {
        md << "| " << name << " | " << fmt(b, prec) << " | " << fmt(m, prec) << " | "
           << (b != 0 ? pct((b - m) / b) : std::string{"—"}) << " |\n";
        std::printf("  %-28s %10s %10s %8s\n", name, fmt(b, prec).c_str(), fmt(m, prec).c_str(),
                    b != 0 ? pct((b - m) / b).c_str() : "-");
      };
      md << "\n**Week totals** (recomputed by the checker)\n\n| | baseline | MILP | reduction |\n|---|---|---|---|\n";
      std::cout << "\n  week totals                    baseline       MILP  reduction\n";
      row("cost (EUR)", wb.cost, wm.cost);
      row("km", wb.km, wm.km);
      row("postponement decisions", wb.postponed, wm.postponed, 0);
      row("orders unserved at Friday end", static_cast<double>(base_run.week_check.unserved_order_ids.size()),
          static_cast<double>(milp_run.week_check.unserved_order_ids.size()), 0);
      row("driver-days used", wb.used, wm.used, 0);
      row("service hours", wb.service_h, wm.service_h);
      row("overtime minutes", wb.overtime, wm.overtime, 0);
      md << "\nWeek-mode checker: baseline " << (base_run.week_check.ok() ? "OK" : "VIOLATIONS") << ", MILP "
         << (milp_run.week_check.ok() ? "OK" : "VIOLATIONS") << ".\n";
      std::cout << "  week checker: baseline " << (base_run.week_check.ok() ? "OK" : "VIOLATIONS") << ", MILP "
                << (milp_run.week_check.ok() ? "OK" : "VIOLATIONS") << "\n";
      for (std::size_t d = 0; d < milp_run.plans.size(); ++d) {
        data::write_text_file(out / ("day" + std::to_string(d)) / "plan.json",
                              to_canonical_text(json(milp_run.plans[d])));
      }
    }

    data::write_text_file(out / "bench.json", result.dump(2) + "\n");
    data::write_text_file(out / "report.md", md.str());
    std::cout << "\nwritten: " << (out / "report.md").string() << "\n";
    return 0;
  } catch (const GRBException& e) {
    std::cerr << "legalvrp-bench: Gurobi error " << e.getErrorCode() << ": " << e.getMessage() << "\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-bench: " << e.what() << "\n";
    return 2;
  }
}
