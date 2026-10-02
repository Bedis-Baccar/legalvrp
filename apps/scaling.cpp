// Scaling experiment: runtime and gap vs number of orders
// Task T9 (PROJECT_BRIEF.md §12). Skeleton: parses arguments, then reports not-implemented.
#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#include "legalvrp/domain/paths.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Scaling experiment: runtime and gap vs number of orders"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  std::filesystem::path config;
  app.add_option("--config", config, "config/instance_scale.yaml")->required()->check(CLI::ExistingFile);
  CLI11_PARSE(app, argc, argv);
  std::cerr << "legalvrp: 'scaling' is not implemented yet (task T9)\n";
  return 2;
}
