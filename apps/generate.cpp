// Generate a synthetic instance (mode A) — tasks T2 + T5 (PROJECT_BRIEF.md §5.1).
//   legalvrp-generate --config config/instance_small.yaml --seed 1 [--out DIR] [--config-dir DIR]
//   legalvrp-generate --config config/instance_large.yaml --size 60 --seed 1   (size families)
//                     [--no-certify] [--max-attempts N]
// Writes DIR/{week.json, matrix.json, truth.json}; default DIR = data/instances/<name>/<seed>.
// Certification (default): the territory baseline is played over the week; the instance is kept
// only if every plan passes the checker and no day postpones more than max_postponed_share.
#include <CLI/CLI.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"
#include "legalvrp/week/certify.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  CLI::App app{"Generate a synthetic instance (mode A)"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  fs::path config;
  std::uint64_t seed = 0;
  fs::path out;
  fs::path config_dir = legalvrp::config_dir();
  bool no_certify = false;
  int max_attempts = 50;
  int size = 0;
  app.add_option("--config", config, "config/instance_<name>.yaml")->required()->check(CLI::ExistingFile);
  app.add_option("--seed", seed, "RNG seed")->required();
  app.add_option("--out", out, "output directory (default data/instances/<name>/<seed>)");
  app.add_option("--config-dir", config_dir, "directory with rules/contracts/costs.yaml")
      ->check(CLI::ExistingDirectory);
  app.add_option("--size", size, "family configs (instance_scale / instance_large): orders per day")
      ->check(CLI::Range(1, 1000));
  app.add_flag("--no-certify", no_certify, "skip baseline certification (T5)");
  app.add_option("--max-attempts", max_attempts, "regeneration attempts before giving up")
      ->check(CLI::Range(1, 1000));
  CLI11_PARSE(app, argc, argv);

  try {
    const auto cfg = size > 0 ? legalvrp::data::scale_instance(legalvrp::data::load_scale_config(config), size)
                              : legalvrp::data::load_instance_config(config);
    const auto rules = legalvrp::load_config(config_dir);

    legalvrp::data::GeneratedWeek gen;
    std::optional<legalvrp::week::BaselineWeek> baseline;
    if (no_certify) {
      gen = legalvrp::data::generate_week(cfg, rules, seed);
    } else {
      auto c = legalvrp::week::generate_certified_week(cfg, rules, seed, max_attempts);
      if (!c) {
        std::cerr << "legalvrp-generate: no certified instance in " << max_attempts
                  << " attempts (max_postponed_share " << cfg.max_postponed_share << ")\n";
        return 1;
      }
      gen = std::move(c->generated);
      baseline = std::move(c->baseline);
    }

    if (out.empty()) {
      out = legalvrp::repo_root() / "data" / "instances" / cfg.name / std::to_string(seed);
    }
    out.make_preferred();
    legalvrp::data::write_week(out, gen.week, gen.truth);

    std::map<int, int> per_day;
    int pallets = 0;
    for (const auto& o : gen.week.orders) {
      ++per_day[o.day];
      pallets += o.pallets;
    }
    std::cout << "instance " << cfg.name << " seed " << seed << " -> " << out.string() << "\n"
              << "  customers " << gen.week.customers.size() << ", drivers "
              << gen.week.drivers.size() << ", orders " << gen.week.orders.size() << " ("
              << pallets << " pallets)\n  orders per day:";
    for (const auto& [d, n] : per_day) std::cout << " d" << d << "=" << n;
    std::cout << "\n";
    if (baseline) {
      std::cout << "  certified at attempt " << gen.week.attempt << ": baseline postponed share per day:";
      for (const double s : baseline->postponed_share) {
        std::cout << " " << std::fixed << std::setprecision(0) << 100.0 * s << "%";
      }
      std::cout << "\n";
    } else {
      std::cout << "  certified: no (--no-certify)\n";
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-generate: " << e.what() << "\n";
    return 2;
  }
}
