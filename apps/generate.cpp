// Generate a synthetic instance (mode A) — task T2 (PROJECT_BRIEF.md §5.1).
//   legalvrp-generate --config config/instance_small.yaml --seed 1 [--out DIR] [--config-dir DIR]
// Writes DIR/{week.json, matrix.json, truth.json}; default DIR = data/instances/<name>/<seed>.
#include <CLI/CLI.hpp>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>

#include "legalvrp/data/generate.hpp"
#include "legalvrp/data/instance_config.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/paths.hpp"
#include "legalvrp/domain/rules.hpp"

namespace fs = std::filesystem;

int main(int argc, char** argv) {
  CLI::App app{"Generate a synthetic instance (mode A)"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  fs::path config;
  std::uint64_t seed = 0;
  fs::path out;
  fs::path config_dir = legalvrp::config_dir();
  app.add_option("--config", config, "config/instance_<name>.yaml")->required()->check(CLI::ExistingFile);
  app.add_option("--seed", seed, "RNG seed")->required();
  app.add_option("--out", out, "output directory (default data/instances/<name>/<seed>)");
  app.add_option("--config-dir", config_dir, "directory with rules/contracts/costs.yaml")
      ->check(CLI::ExistingDirectory);
  CLI11_PARSE(app, argc, argv);

  try {
    const auto cfg = legalvrp::data::load_instance_config(config);
    const auto rules = legalvrp::load_config(config_dir);
    const auto gen = legalvrp::data::generate_week(cfg, rules, seed);
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
    std::cout << "\n  certified: no (baseline certification arrives with T5)\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-generate: " << e.what() << "\n";
    return 1;
  }
}
