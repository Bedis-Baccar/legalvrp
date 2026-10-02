// Independent legality checker — task T4 (PROJECT_BRIEF.md §8).
//   legalvrp-check --instance data/instances/small/1 --plans results/<run>     (week mode)
//   legalvrp-check --instance data/instances/small/1 --plan plan.json           (one day)
// Week mode reads <plans>/day<d>/plan.json for every day. A single plan is checked as a fresh
// day: no carried orders, zero weekly state. Exit code 0 = legal, 1 = violations, 2 = error.
#include <CLI/CLI.hpp>

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "legalvrp/check/checker.hpp"
#include "legalvrp/data/io.hpp"
#include "legalvrp/domain/day.hpp"
#include "legalvrp/domain/json.hpp"
#include "legalvrp/domain/paths.hpp"

namespace fs = std::filesystem;

namespace {
legalvrp::DayPlan read_plan(const fs::path& file) {
  return nlohmann::json::parse(legalvrp::data::read_text_file(file)).get<legalvrp::DayPlan>();
}
void print(const std::vector<legalvrp::Violation>& vs) {
  for (const auto& v : vs) {
    std::cout << "  ";
    if (v.day >= 0) std::cout << "day " << v.day << "  ";
    std::cout << v.rule;
    if (!v.driver_id.empty()) std::cout << "  driver " << v.driver_id;
    if (!v.order_id.empty()) std::cout << "  order " << v.order_id;
    if (v.amount != 0.0) std::cout << "  amount " << v.amount;
    std::cout << "\n";
  }
}
}  // namespace

int main(int argc, char** argv) {
  CLI::App app{"Independent legality checker"};
  app.set_version_flag("--version", std::string{legalvrp::kVersion});
  fs::path instance;
  fs::path plan;
  fs::path plans;
  app.add_option("--instance", instance, "instance directory (week.json, matrix.json)")
      ->required()->check(CLI::ExistingDirectory);
  auto* one = app.add_option("--plan", plan, "one day plan.json")->check(CLI::ExistingFile);
  auto* all = app.add_option("--plans", plans, "directory with day<d>/plan.json")
                  ->check(CLI::ExistingDirectory);
  one->excludes(all);
  all->excludes(one);
  CLI11_PARSE(app, argc, argv);
  if (plan.empty() && plans.empty()) {
    std::cerr << "legalvrp-check: give --plan or --plans\n";
    return 2;
  }

  try {
    const auto week = legalvrp::data::read_week(instance);
    if (!plan.empty()) {
      const auto p = read_plan(plan);
      const auto day = legalvrp::make_day_instance(week, p.day);
      const auto r = legalvrp::check::check_day(day, p);
      std::cout << "day " << p.day << ": " << r.violations.size() << " violation(s), objective "
                << r.objective << "\n";
      print(r.violations);
      return r.ok() ? 0 : 1;
    }
    std::vector<legalvrp::DayPlan> ps;
    for (int d = 0; d < week.days; ++d) ps.push_back(read_plan(plans / ("day" + std::to_string(d)) / "plan.json"));
    const auto r = legalvrp::check::check_week(week, ps);
    std::cout << "week: " << r.violations.size() << " violation(s), " << r.unserved_order_ids.size()
              << " order(s) unserved at the end\n";
    print(r.violations);
    return r.ok() ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "legalvrp-check: " << e.what() << "\n";
    return 2;
  }
}
