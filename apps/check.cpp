// Independent legality checker
// Task T4 (PROJECT_BRIEF.md §12). Skeleton: parses arguments, then reports not-implemented.
#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#include "legalvrp/domain/paths.hpp"

int main(int argc, char** argv) {
  CLI::App app{"Independent legality checker"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  std::filesystem::path instance;
  std::filesystem::path plan;
  app.add_option("--instance", instance, "instance file or directory")->required()->check(CLI::ExistingPath);
  app.add_option("--plan", plan, "plan.json")->required()->check(CLI::ExistingFile);
  CLI11_PARSE(app, argc, argv);
  std::cerr << "legalvrp: 'check' is not implemented yet (task T4)\n";
  return 2;
}
