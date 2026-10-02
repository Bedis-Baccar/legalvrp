// Run the rolling Monday-Friday week
// Task T8 (PROJECT_BRIEF.md §12). Skeleton: parses arguments, then reports not-implemented.
#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#include "legalvrp/domain/paths.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Run the rolling Monday-Friday week"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  std::filesystem::path instance;
  app.add_option("--instance", instance, "data/instances/<name>/<seed>")->required()->check(CLI::ExistingDirectory);
  CLI11_PARSE(app, argc, argv);
  std::cerr << "legalvrp: 'run_week' is not implemented yet (task T8)\n";
  return 2;
}
