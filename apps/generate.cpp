// Generate a synthetic instance (mode A)
// Task T2 (PROJECT_BRIEF.md §12). Skeleton: parses arguments, then reports not-implemented.
#include <CLI/CLI.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "legalvrp/domain/paths.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Generate a synthetic instance (mode A)"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  std::filesystem::path config;
  std::uint64_t seed = 0;
  app.add_option("--config", config, "config/instance_<name>.yaml")->required()->check(CLI::ExistingFile);
  app.add_option("--seed", seed, "RNG seed")->required();
  CLI11_PARSE(app, argc, argv);
  std::cerr << "legalvrp: 'generate' is not implemented yet (task T2)\n";
  return 2;
}
